// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_TEXT_EDIT_COMMANDS_H_
#define UI_BASE_IME_TEXT_EDIT_COMMANDS_H_

namespace ui {

// Text editing commands for use by ui::TextInputClient.
enum class TextEditCommand {
  FIRST_COMMAND = 0,
  DELETE_BACKWARD = FIRST_COMMAND,
  DELETE_FORWARD,
  DELETE_TO_BEGINNING_OF_LINE,
  DELETE_TO_BEGINNING_OF_PARAGRAPH,
  DELETE_TO_END_OF_LINE,
  DELETE_TO_END_OF_PARAGRAPH,
  DELETE_WORD_BACKWARD,
  DELETE_WORD_FORWARD,
  MOVE_BACKWARD,
  MOVE_BACKWARD_AND_MODIFY_SELECTION,
  MOVE_DOWN,
  MOVE_DOWN_AND_MODIFY_SELECTION,
  MOVE_FORWARD,
  MOVE_FORWARD_AND_MODIFY_SELECTION,
  MOVE_LEFT,
  MOVE_LEFT_AND_MODIFY_SELECTION,
  MOVE_PAGE_DOWN,
  MOVE_PAGE_DOWN_AND_MODIFY_SELECTION,
  MOVE_PAGE_UP,
  MOVE_PAGE_UP_AND_MODIFY_SELECTION,
  MOVE_RIGHT,
  MOVE_RIGHT_AND_MODIFY_SELECTION,
  MOVE_TO_BEGINNING_OF_DOCUMENT,
  MOVE_TO_BEGINNING_OF_DOCUMENT_AND_MODIFY_SELECTION,
  MOVE_TO_BEGINNING_OF_LINE,
  MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION,
  MOVE_TO_BEGINNING_OF_PARAGRAPH,
  MOVE_TO_BEGINNING_OF_PARAGRAPH_AND_MODIFY_SELECTION,
  MOVE_TO_END_OF_DOCUMENT,
  MOVE_TO_END_OF_DOCUMENT_AND_MODIFY_SELECTION,
  MOVE_TO_END_OF_LINE,
  MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION,
  MOVE_TO_END_OF_PARAGRAPH,
  MOVE_TO_END_OF_PARAGRAPH_AND_MODIFY_SELECTION,
  MOVE_PARAGRAPH_BACKWARD_AND_MODIFY_SELECTION,
  MOVE_PARAGRAPH_FORWARD_AND_MODIFY_SELECTION,
  MOVE_UP,
  MOVE_UP_AND_MODIFY_SELECTION,
  MOVE_WORD_BACKWARD,
  MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION,
  MOVE_WORD_FORWARD,
  MOVE_WORD_FORWARD_AND_MODIFY_SELECTION,
  MOVE_WORD_LEFT,
  MOVE_WORD_LEFT_AND_MODIFY_SELECTION,
  MOVE_WORD_RIGHT,
  MOVE_WORD_RIGHT_AND_MODIFY_SELECTION,
  UNDO,
  REDO,
  CUT,
  COPY,
  PASTE,
  SELECT_ALL,
  TRANSPOSE,
  YANK,
  INSERT_TEXT,
  SET_MARK,
  UNSELECT,

  // LAST_COMMAND must be the last one. Add new command before it.
  LAST_COMMAND,
  INVALID_COMMAND = LAST_COMMAND,
};

}  // namespace ui

#endif  // UI_BASE_IME_TEXT_EDIT_COMMANDS_H_
