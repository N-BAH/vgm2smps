#include "getTerminalWidth.hxx"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

auto getTerminalWidth(unsigned defaultWidth) -> unsigned {
#ifdef _WIN32
  // Ветка для Windows
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  // Получаем хэндл стандартного вывода
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

  if (hConsole != INVALID_HANDLE_VALUE and
      GetConsoleScreenBufferInfo(hConsole, &csbi) == TRUE) {
    // Вычисляем ширину окна на основе координат видимой области (окна)
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;

    // Альтернативно, если нужна ширина ВСЕГО буфера прокрутки, используют:
    // return csbi.dwSize.X;
  }
#else
  // Ветка для Linux / macOS
  struct winsize w;
  // Делаем системный запрос к стандартному выводу
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    return w.ws_col;  // Количество колонок (символов в ширину)
  }
#endif

  return defaultWidth;  // Фолбэк, если дескриптор не терминал
}