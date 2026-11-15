#pragma once
#include <functional>
namespace BuiltinPlugins::Translation { class TranslationController; }
namespace SigilAgent {
class ToolRegistry;
void registerTranslationTools(ToolRegistry *registry, BuiltinPlugins::Translation::TranslationController *controller,
                              std::function<void()> started = {});
}
