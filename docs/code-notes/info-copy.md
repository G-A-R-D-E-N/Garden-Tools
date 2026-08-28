# INFO copy implementation notes

## Radicle/src/schema/form_links.cpp:visit_link_offsets

Record-aware classification is applied before generic signature-only handling. Strict cross-context operations reject unclassified fields, while identical contexts use a no-op path. The field-specific requirements are covered by the implementation tests and task audit.

## Radicle/src/forms/placed_reference_copy.cpp:copy_info_as_new

The contained copy validates source and destination ancestry before committing a working copy. Child-group creation and record insertion remain separate so failure behavior and ordering are tested independently.
