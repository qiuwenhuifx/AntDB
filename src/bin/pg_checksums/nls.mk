# src/bin/pg_checksums/nls.mk
CATALOG_NAME     = pg_checksums
AVAIL_LANGUAGES  = cs de es fr ja ko ru sv tr uk
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) pg_checksums.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS)
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS)
