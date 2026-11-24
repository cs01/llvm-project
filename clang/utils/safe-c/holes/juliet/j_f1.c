#include <wchar.h>
int main(void) {
  wchar_t buf[40] = L"%s%s%s%s%s%s%s%s%n%n";
  wchar_t *data = buf;
  wprintf(data);
  return 0;
}
