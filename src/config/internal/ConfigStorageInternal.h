#pragma once

// Внутренние помощники хранения конфига и операций с программами.
// Нельзя подключать вне подсистемы config.

namespace config_storage_internal {
void prepareFlashWriteLogGcAndWdt();
bool ensureProgramsDir();
} // namespace config_storage_internal

