#ifndef IMPRINT_SETUP_MERGE_H
#define IMPRINT_SETUP_MERGE_H

#include "document.h"

extern const char *const SETUP_REMOVE_KEYS[];
extern const size_t SETUP_REMOVE_KEY_COUNT;

document_node *setup_merge(const document_node *base, const document_node *child, error_list *errors);
void setup_apply_removes(document_node *merged, const document_node *removes);

#endif
