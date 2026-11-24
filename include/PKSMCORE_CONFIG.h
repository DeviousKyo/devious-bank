// Build configuration for PKSM-Core (see lib/PKSM-Core/README.md).
#ifndef PKSMCORE_CONFIG_H
#define PKSMCORE_CONFIG_H

// Language string tables: romfs:/i18n/<lang>/<table>.txt
#define _PKSMCORE_LANG_FOLDER "romfs:/i18n/"
// Species personal tables: romfs:/personal/personalN
#define _PKSMCORE_PERSONAL_FOLDER "romfs:/personal/"
// devkitARM's newlib exposes getline as __getline.
#define _PKSMCORE_GETLINE_FUNC __getline
// Devious Bank only touches PKSM-Core from the main thread.
#define _PKSMCORE_DISABLE_THREAD_SAFETY

#endif
