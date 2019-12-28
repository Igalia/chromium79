#!/usr/bin/env python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Implements a simple "negative compile" test for C++ on linux.

Sometimes a C++ API needs to ensure that various usages cannot compile. To
enable unittesting of these assertions, we use this python script to
invoke the compiler on a source file and assert that compilation fails.

For more info, see:
  http://dev.chromium.org/developers/testing/no-compile-tests
"""

from __future__ import print_function

import StringIO
import ast
import os
import re
import select
import subprocess
import sys
import tempfile
import time


# Matches lines that start with #if and have the substring TEST in the
# conditional. Also extracts the comment.  This allows us to search for
# lines like the following:
#
#   #ifdef NCTEST_NAME_OF_TEST  // [r'expected output']
#   #if defined(NCTEST_NAME_OF_TEST)  // [r'expected output']
#   #if NCTEST_NAME_OF_TEST  // [r'expected output']
#   #elif NCTEST_NAME_OF_TEST  // [r'expected output']
#   #elif DISABLED_NCTEST_NAME_OF_TEST  // [r'expected output']
#
# inside the unittest file.
NCTEST_CONFIG_RE = re.compile(r'^#(?:el)?if.*\s+(\S*NCTEST\S*)\s*(//.*)?')


# Matches and removes the defined() preprocesor predicate. This is useful
# for test cases that use the preprocessor if-statement form:
#
#   #if defined(NCTEST_NAME_OF_TEST)
#
# Should be used to post-process the results found by NCTEST_CONFIG_RE.
STRIP_DEFINED_RE = re.compile(r'defined\((.*)\)')


# Used to grab the expectation from comment at the end of an #ifdef.  See
# NCTEST_CONFIG_RE's comment for examples of what the format should look like.
#
# The extracted substring should be a python array of regular expressions.
EXTRACT_EXPECTATION_RE = re.compile(r'//\s*(\[.*\])')


# The header for the result file so that it can be compiled.
RESULT_FILE_HEADER = """
// This file is generated by the no compile test from:
//   %s

#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

"""


# The log message on a test completion.
LOG_TEMPLATE = """
TEST(%s, %s) took %f secs. Started at %f, ended at %f.
"""

# The GUnit test function to output for a successful or disabled test.
GUNIT_TEMPLATE = """
TEST(%s, %s) { }
"""


# Timeout constants.
NCTEST_TERMINATE_TIMEOUT_SEC = 120
NCTEST_KILL_TIMEOUT_SEC = NCTEST_TERMINATE_TIMEOUT_SEC + 2
BUSY_LOOP_MAX_TIME_SEC = NCTEST_KILL_TIMEOUT_SEC * 2


def ValidateInput(compiler, parallelism, sourcefile_path, cflags,
                  resultfile_path):
  """Make sure the arguments being passed in are sane."""
  assert os.path.isfile(compiler)
  assert parallelism >= 1
  assert type(sourcefile_path) is str
  assert type(cflags) is list
  for flag in cflags:
    assert type(flag) is str
  assert type(resultfile_path) is str


def ParseExpectation(expectation_string):
  """Extracts expectation definition from the trailing comment on the ifdef.

  See the comment on NCTEST_CONFIG_RE for examples of the format we are parsing.

  Args:
    expectation_string: A string like "// [r'some_regex']"

  Returns:
    A list of compiled regular expressions indicating all possible valid
    compiler outputs.  If the list is empty, all outputs are considered valid.
  """
  assert expectation_string is not None

  match = EXTRACT_EXPECTATION_RE.match(expectation_string)
  assert match

  raw_expectation = ast.literal_eval(match.group(1))
  assert type(raw_expectation) is list

  expectation = []
  for regex_str in raw_expectation:
    assert type(regex_str) is str
    expectation.append(re.compile(regex_str))
  return expectation


def ExtractTestConfigs(sourcefile_path, suite_name):
  """Parses the source file for test configurations.

  Each no-compile test in the file is separated by an ifdef macro.  We scan
  the source file with the NCTEST_CONFIG_RE to find all ifdefs that look like
  they demark one no-compile test and try to extract the test configuration
  from that.

  Args:
    sourcefile_path: The path to the source file.
    suite_name: The name of the test suite.

  Returns:
    A list of test configurations. Each test configuration is a dictionary of
    the form:

      { name: 'NCTEST_NAME'
        suite_name: 'SOURCE_FILE_NAME'
        expectations: [re.Pattern, re.Pattern] }

    The |suite_name| is used to generate a pretty gtest output on successful
    completion of the no compile test.

    The compiled regexps in |expectations| define the valid outputs of the
    compiler.  If any one of the listed patterns matches either the stderr or
    stdout from the compilation, and the compilation failed, then the test is
    considered to have succeeded.  If the list is empty, than we ignore the
    compiler output and just check for failed compilation. If |expectations|
    is actually None, then this specifies a compiler sanity check test, which
    should expect a SUCCESSFUL compilation.
  """
  sourcefile = open(sourcefile_path, 'r')

  # Start with at least the compiler sanity test.  You need to always have one
  # sanity test to show that compiler flags and configuration are not just
  # wrong.  Otherwise, having a misconfigured compiler, or an error in the
  # shared portions of the .nc file would cause all tests to erroneously pass.
  test_configs = []

  for line in sourcefile:
    match_result = NCTEST_CONFIG_RE.match(line)
    if not match_result:
      continue

    groups = match_result.groups()

    # Grab the name and remove the defined() predicate if there is one.
    name = groups[0]
    strip_result = STRIP_DEFINED_RE.match(name)
    if strip_result:
      name = strip_result.group(1)

    # Read expectations if there are any.
    test_configs.append({'name': name,
                         'suite_name': suite_name,
                         'expectations': ParseExpectation(groups[1])})
  sourcefile.close()
  return test_configs


def StartTest(compiler, sourcefile_path, tempfile_dir, cflags, config):
  """Start one negative compile test.

  Args:
    sourcefile_path: The path to the source file.
    tempfile_dir: A directory to store temporary data from tests.
    cflags: An array of strings with all the CFLAGS to give to gcc.
    config: A dictionary describing the test.  See ExtractTestConfigs
      for a description of the config format.

  Returns:
    A dictionary containing all the information about the started test. The
    fields in the dictionary are as follows:
      { 'proc': A subprocess object representing the compiler run.
        'cmdline': The executed command line.
        'name': The name of the test.
        'suite_name': The suite name to use when generating the gunit test
                      result.
        'terminate_timeout': The timestamp in seconds since the epoch after
                             which the test should be terminated.
        'kill_timeout': The timestamp in seconds since the epoch after which
                        the test should be given a hard kill signal.
        'started_at': A timestamp in seconds since the epoch for when this test
                      was started.
        'aborted_at': A timestamp in seconds since the epoch for when this test
                      was aborted.  If the test completed successfully,
                      this value is 0.
        'finished_at': A timestamp in seconds since the epoch for when this
                       test was successfully complete.  If the test is aborted,
                       or running, this value is 0.
        'expectations': A dictionary with the test expectations. See
                        ParseExpectation() for the structure.
        }
  """
  cmdline = [compiler]
  cmdline.extend(cflags)
  name = config['name']
  expectations = config['expectations']
  if expectations is not None:
    cmdline.append('-D%s' % name)
  cmdline.extend(['-o', '/dev/null', '-c', '-x', 'c++',
                  sourcefile_path])
  test_stdout = tempfile.TemporaryFile(dir=tempfile_dir)
  test_stderr = tempfile.TemporaryFile(dir=tempfile_dir)

  process = subprocess.Popen(cmdline, stdout=test_stdout, stderr=test_stderr)
  now = time.time()
  return {'proc': process,
          'cmdline': ' '.join(cmdline),
          'stdout': test_stdout,
          'stderr': test_stderr,
          'name': name,
          'suite_name': config['suite_name'],
          'terminate_timeout': now + NCTEST_TERMINATE_TIMEOUT_SEC,
          'kill_timeout': now + NCTEST_KILL_TIMEOUT_SEC,
          'started_at': now,
          'aborted_at': 0,
          'finished_at': 0,
          'expectations': expectations}


def PassTest(resultfile, resultlog, test):
  """Logs the result of a test started by StartTest(), or a disabled test
  configuration.

  Args:
    resultfile: File object for .cc file that results are written to.
    resultlog: File object for the log file.
    test: An instance of the dictionary returned by StartTest(), a
          configuration from ExtractTestConfigs().
  """
  resultfile.write(GUNIT_TEMPLATE % (
      test['suite_name'], test['name']))

  # The 'started_at' key is only added if a test has been started.
  if 'started_at' in test:
    resultlog.write(LOG_TEMPLATE % (
        test['suite_name'], test['name'],
        test['finished_at'] - test['started_at'],
        test['started_at'], test['finished_at']))


def FailTest(resultfile, test, error, stdout=None, stderr=None):
  """Logs the result of a test started by StartTest()

  Args:
    resultfile: File object for .cc file that results are written to.
    test: An instance of the dictionary returned by StartTest()
    error: The printable reason for the failure.
    stdout: The test's output to stdout.
    stderr: The test's output to stderr.
  """
  resultfile.write('#error "%s Failed: %s"\n' % (test['name'], error))
  resultfile.write('#error "compile line: %s"\n' % test['cmdline'])
  if stdout and len(stdout) != 0:
    resultfile.write('#error "%s stdout:"\n' % test['name'])
    for line in stdout.split('\n'):
      resultfile.write('#error "  %s:"\n' % line)

  if stderr and len(stderr) != 0:
    resultfile.write('#error "%s stderr:"\n' % test['name'])
    for line in stderr.split('\n'):
      resultfile.write('#error "  %s"\n' % line)
  resultfile.write('\n')


def WriteStats(resultlog, suite_name, timings):
  """Logs the peformance timings for each stage of the script.

  Args:
    resultlog: File object for the log file.
    suite_name: The name of the GUnit suite this test belongs to.
    timings: Dictionary with timestamps for each stage of the script run.
  """
  stats_template = """
TEST(%s): Started %f, Ended %f, Total %fs, Extract %fs, Compile %fs, Process %fs
"""
  total_secs = timings['results_processed'] - timings['started']
  extract_secs = timings['extract_done'] - timings['started']
  compile_secs = timings['compile_done'] - timings['extract_done']
  process_secs = timings['results_processed'] - timings['compile_done']
  resultlog.write(stats_template % (
      suite_name, timings['started'], timings['results_processed'], total_secs,
      extract_secs, compile_secs, process_secs))

def ExtractTestOutputAndCleanup(test):
  """Test output is in temp files. Read those and delete them.
  Returns: A tuple (stderr, stdout).
  """
  outputs = [None, None]
  for i, stream_name in ((0, "stdout"), (1, "stderr")):
    stream = test[stream_name]
    stream.seek(0)
    outputs[i] = stream.read()
    stream.close()

  return outputs

def ProcessTestResult(resultfile, resultlog, test):
  """Interprets and logs the result of a test started by StartTest()

  Args:
    resultfile: File object for .cc file that results are written to.
    resultlog: File object for the log file.
    test: The dictionary from StartTest() to process.
  """
  proc = test['proc']
  proc.wait()
  (stdout, stderr) = ExtractTestOutputAndCleanup(test)

  if test['aborted_at'] != 0:
    FailTest(resultfile, test, "Compile timed out. Started %f ended %f." %
             (test['started_at'], test['aborted_at']))
    return

  if proc.poll() == 0:
    # Handle failure due to successful compile.
    FailTest(resultfile, test,
             'Unexpected successful compilation.',
             stdout, stderr)
    return
  else:
    # Check the output has the right expectations.  If there are no
    # expectations, then we just consider the output "matched" by default.
    if len(test['expectations']) == 0:
      PassTest(resultfile, resultlog, test)
      return

    # Otherwise test against all expectations.
    for regexp in test['expectations']:
      if (regexp.search(stdout) is not None or
          regexp.search(stderr) is not None):
        PassTest(resultfile, resultlog, test)
        return
    expectation_str = ', '.join(
        ["r'%s'" % regexp.pattern for regexp in test['expectations']])
    FailTest(resultfile, test,
             'Expectations [%s] did not match output.' % expectation_str,
             stdout, stderr)
    return


def CompleteAtLeastOneTest(executing_tests):
  """Blocks until at least one task is removed from executing_tests.

  This function removes completed tests from executing_tests, logging failures
  and output.  If no tests can be removed, it will enter a poll-loop until one
  test finishes or times out.  On a timeout, this function is responsible for
  terminating the process in the appropriate fashion.

  Args:
    executing_tests: A dict mapping a string containing the test name to the
                     test dict return from StartTest().

  Returns:
    A list of tests that have finished.
  """
  finished_tests = []
  busy_loop_timeout = time.time() + BUSY_LOOP_MAX_TIME_SEC
  while len(finished_tests) == 0:
    # If we don't make progress for too long, assume the code is just dead.
    assert busy_loop_timeout > time.time()

    # Select on the output files to block until we have something to
    # do. We ignore the return value from select and just poll all
    # processes.
    read_set = []
    for test in executing_tests.values():
      read_set.extend([test['stdout'], test['stderr']])
    select.select(read_set, [], read_set, NCTEST_TERMINATE_TIMEOUT_SEC)

    # Now attempt to process results.
    now = time.time()
    for test in executing_tests.values():
      proc = test['proc']
      if proc.poll() is not None:
        test['finished_at'] = now
        finished_tests.append(test)
      elif test['terminate_timeout'] < now:
        proc.terminate()
        test['aborted_at'] = now
      elif test['kill_timeout'] < now:
        proc.kill()
        test['aborted_at'] = now

    if len(finished_tests) == 0:
      # We had output from some process but no process had
      # finished. To avoid busy looping while waiting for a process to
      # finish, insert a small 100 ms delay here.
      time.sleep(0.1)

  for test in finished_tests:
    del executing_tests[test['name']]
  return finished_tests


def main():
  if len(sys.argv) < 6 or sys.argv[5] != '--':
    print('Usage: %s <compiler> <parallelism> <sourcefile> <resultfile> '
          '-- <cflags...>' % sys.argv[0])
    sys.exit(1)

  # Force us into the "C" locale so the compiler doesn't localize its output.
  # In particular, this stops gcc from using smart quotes when in english UTF-8
  # locales.  This makes the expectation writing much easier.
  os.environ['LC_ALL'] = 'C'

  compiler = sys.argv[1]
  parallelism = int(sys.argv[2])
  sourcefile_path = sys.argv[3]
  resultfile_path = sys.argv[4]
  cflags = sys.argv[6:]

  timings = {'started': time.time()}

  ValidateInput(compiler, parallelism, sourcefile_path, cflags, resultfile_path)

  # Convert filename from underscores to CamelCase.
  words = os.path.splitext(os.path.basename(sourcefile_path))[0].split('_')
  words = [w.capitalize() for w in words]
  suite_name = 'NoCompile' + ''.join(words)

  test_configs = ExtractTestConfigs(sourcefile_path, suite_name)
  timings['extract_done'] = time.time()

  resultfile = StringIO.StringIO()
  resultlog = StringIO.StringIO()
  resultfile.write(RESULT_FILE_HEADER % sourcefile_path)

  # Run the no-compile tests, but ensure we do not run more than |parallelism|
  # tests at once.
  timings['header_written'] = time.time()
  executing_tests = {}
  finished_tests = []

  cflags.extend(['-MMD', '-MF', resultfile_path + '.d', '-MT', resultfile_path])
  test = StartTest(
      compiler,
      sourcefile_path,
      os.path.dirname(resultfile_path),
      cflags,
      { 'name': 'NCTEST_SANITY',
        'suite_name': suite_name,
        'expectations': None,
      })
  executing_tests[test['name']] = test

  for config in test_configs:
    # CompleteAtLeastOneTest blocks until at least one test finishes. Thus, this
    # acts as a semaphore.  We cannot use threads + a real semaphore because
    # subprocess forks, which can cause all sorts of hilarity with threads.
    if len(executing_tests) >= parallelism:
      finished_tests.extend(CompleteAtLeastOneTest(executing_tests))

    if config['name'].startswith('DISABLED_'):
      PassTest(resultfile, resultlog, config)
    else:
      test = StartTest(compiler, sourcefile_path,
                       os.path.dirname(resultfile_path), cflags, config)
      assert test['name'] not in executing_tests
      executing_tests[test['name']] = test

  # If there are no more test to start, we still need to drain the running
  # ones.
  while len(executing_tests) > 0:
    finished_tests.extend(CompleteAtLeastOneTest(executing_tests))
  timings['compile_done'] = time.time()

  finished_tests = sorted(finished_tests, key=lambda test: test['name'])
  for test in finished_tests:
    if test['name'] == 'NCTEST_SANITY':
      test['proc'].wait()
      (stdout, stderr) = ExtractTestOutputAndCleanup(test)
      return_code = test['proc'].returncode
      if return_code != 0:
        sys.stdout.write(stdout)
        sys.stderr.write(stderr)
      continue
    ProcessTestResult(resultfile, resultlog, test)
  timings['results_processed'] = time.time()

  WriteStats(resultlog, suite_name, timings)

  with open(resultfile_path + '.log', 'w') as fd:
    fd.write(resultlog.getvalue())
  if return_code == 0:
    with open(resultfile_path, 'w') as fd:
      fd.write(resultfile.getvalue())

  resultfile.close()
  if return_code != 0:
    print("No-compile driver failure with return_code %d. Result log:" %
          return_code)
    print(resultlog.getvalue())
  sys.exit(return_code)


if __name__ == '__main__':
  main()
