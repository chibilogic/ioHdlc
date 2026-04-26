/*
 * ioHdlc Shell Configuration
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
/**
 * @file    shellconf.h
 * @brief   Shell configuration for ioHdlc tests.
 */

#ifndef SHELLCONF_H
#define SHELLCONF_H

/*===========================================================================*/
/* Module pre-compile time settings.                                         */
/*===========================================================================*/

/**
 * @brief   Shell maximum input line length.
 */
#define SHELL_MAX_LINE_LENGTH       128

/**
 * @brief   Shell maximum arguments per command.
 */
#define SHELL_MAX_ARGUMENTS         16

/**
 * @brief   Shell maximum command history buffer.
 */
#define SHELL_MAX_HIST_BUFF         8 * SHELL_MAX_LINE_LENGTH

/**
 * @brief   Enable shell command history
 */
#define SHELL_USE_HISTORY           TRUE

/**
 * @brief   Enable shell command completion
 */
#define SHELL_USE_COMPLETION        FALSE

/**
 * @brief   Shell Maximum Completions
 */
#define SHELL_MAX_COMPLETIONS       8

/**
 * @brief   Enable shell escape sequence processing
 */
#define SHELL_USE_ESC_SEQ           TRUE

/**
 * @brief   Prompt string
 */
#define SHELL_PROMPT_STR            "iohdlc> "

/**
 * @brief   Newline string
 */
#define SHELL_NEWLINE_STR           "\r\n"

/**
 * @brief   Default shell thread name.
 */
#define SHELL_THREAD_NAME           "shell"

/*===========================================================================*/
/* Shell Command Enables                                                     */
/*===========================================================================*/

/**
 * @brief   Enable exit command
 */
#define SHELL_CMD_EXIT_ENABLED      TRUE

/**
 * @brief   Enable info command
 */
#define SHELL_CMD_INFO_ENABLED      TRUE

/**
 * @brief   Enable echo command
 */
#define SHELL_CMD_ECHO_ENABLED      TRUE

/**
 * @brief   Enable systime command
 */
#define SHELL_CMD_SYSTIME_ENABLED   TRUE

/**
 * @brief   Enable mem command
 */
#define SHELL_CMD_MEM_ENABLED       TRUE

/**
 * @brief   Enable threads command
 */
#define SHELL_CMD_THREADS_ENABLED   TRUE

/**
 * @brief   Disable ChibiOS internal test command
 */
#define SHELL_CMD_TEST_ENABLED      FALSE

/**
 * @brief   Disable file system commands
 */
#define SHELL_CMD_FILES_ENABLED     FALSE

#endif /* SHELLCONF_H */
