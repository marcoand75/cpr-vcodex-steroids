#pragma once

#include <atomic>
#include <string>
#include <vector>

class GfxRenderer;
class MappedInputManager;

class EndOfBookOptions {
 public:
  enum class Action { None, Redraw, OpenBook, GoHome, LastPage };
  static constexpr size_t MAX_SUGGESTIONS = 3;

  void loadOnce(const std::string& currentBookPath);
  bool menuActive() const;
  Action handleMenuInput(const MappedInputManager& input, std::string* openPath);
  void render(GfxRenderer& renderer, const MappedInputManager& input) const;

 private:
  std::string folder;
  std::vector<std::string> names;
  int selector = 0;
  std::atomic<bool> isLoaded{false};

  std::string fullPath(size_t index) const;
};
