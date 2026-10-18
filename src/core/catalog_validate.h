#ifndef IMPRINT_CATALOG_VALIDATE_H
#define IMPRINT_CATALOG_VALIDATE_H

#include "catalog.h"

bool catalog_validate_app(const document_node *entry, error_list *errors);
bool catalog_validate_distro(const document_node *entry, error_list *errors);
bool catalog_validate_window_manager(const document_node *entry, error_list *errors);
bool catalog_validate_bootloader(const document_node *entry, error_list *errors);
bool catalog_check_references(const catalog *loaded, error_list *errors);

#endif
