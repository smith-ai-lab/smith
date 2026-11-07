// Generated split of smith.c (#2685): what runtime/lang_type.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_fields.h"
#include "lang_main.h"

void smith_lang_ty_reset(void);

unsigned smith_lang_ty_depth_of(uint64_t handle);

const char *smith_lang_ty_spelling_of(SmithLang *lang, uint64_t handle);

bool smith_lang_ty_stem_spelling(const char *stem, size_t length, char *out, size_t capacity);

bool smith_lang_ty_handle(SmithLang *lang, const SmithHost *host, const char *spelling, uint32_t location,
                                 uint64_t *handle);

const char *smith_lang_ty_second_level(void);

size_t smith_lang_ty_pieces(uint64_t handle, char pieces[][smith_lang_ty_piece_size]);

SmithTokenKind smith_lang_ty_piece_kind(const char *piece);

bool smith_lang_ty_literal(uint64_t handle, char *out, size_t capacity);

extern SmithLang *smith_lang_ty_lang;

size_t smith_lang_ty_type_tokens(SmithLang *lang, uint64_t handle, const SmithToken *like, SmithToken *out,
                                        size_t capacity, bool bare);

void smith_lang_ty_attribute_taken(void);

bool smith_lang_ty_token(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_types_define(SmithLang *lang, const SmithHost *host, uint32_t location);

void smith_lang_field_after_imports(SmithLang *lang, const SmithHost *host);

bool smith_lang_slice_tag_definition(const char *tag, char *data, size_t capacity, unsigned long *count);

