#pragma once
#include <Windows.h>

namespace yinamp::app {
class Application final {
  public:
    int Run(HINSTANCE instance, int showCommand);
};
}
