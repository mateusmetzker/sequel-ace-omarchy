//
//  SPEditorTokens.h
//  sequel-pro / Sequel Ace (Linux port)
//
//  Token identifiers produced by the SQL editor lexer (SPEditorTokens.l), plus
//  the small C API the Linux port uses to drive the lexer from C++.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#ifndef SP_EDITOR_TOKENS_H
#define SP_EDITOR_TOKENS_H

#include <stddef.h>

#define SPT_DOUBLE_QUOTED_TEXT   1
#define SPT_SINGLE_QUOTED_TEXT   2
#define SPT_COMMENT              3
#define SPT_BACKTICK_QUOTED_TEXT 4
#define SPT_RESERVED_WORD        5
#define SPT_WHITESPACE           6
#define SPT_NUMERIC              7
#define SPT_VARIABLE             8
#define SPT_WORD                 9
#define SPT_OTHER               10

#ifdef __cplusplus
extern "C" {
#endif

/* Start scanning a UTF-8 byte span. Any previous scan is discarded. */
void SPEditorTokensScan(const char *bytes, size_t byteLength);
/* Same, but the scanner starts inside a C-style block comment. */
void SPEditorTokensScanInComment(const char *bytes, size_t byteLength);
/* Returns the next SPT_* token, or 0 at the end of the input. */
int SPEditorTokensNext(void);
/* Offset (in UTF-16 code units) of the token returned by the last SPEditorTokensNext call. */
size_t SPEditorTokensOffset(void);
/* Length (in UTF-16 code units) of the token returned by the last SPEditorTokensNext call. */
size_t SPEditorTokensLength(void);
/* Raw bytes of the last token (not NUL-terminated beyond byte length). */
const char *SPEditorTokensText(void);
size_t SPEditorTokensTextByteLength(void);
/* After SPEditorTokensNext returned 0: whether the input ended inside a block comment. */
int SPEditorTokensEndedInsideComment(void);
/* Discard the current scan early (only needed when not scanning to the end). */
void SPEditorTokensAbort(void);

#ifdef __cplusplus
}
#endif

#endif /* SP_EDITOR_TOKENS_H */
