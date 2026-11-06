; inherits: c

; asm`...` at module level is assembly (D7, #2636): the tag names the language. The text starts after `asm` and the
; backtick and ends before the closing backtick.
((tagged_literal) @injection.content
  (#match? @injection.content "^asm`")
  (#set! injection.language "asm")
  (#offset! @injection.content 0 4 0 -1))
