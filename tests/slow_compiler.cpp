#include <windows.h>

// 子プロセス実行中の取消し・終了待ちを決定的に検証する。製品には配置しない。
int main()
{
    Sleep(30000);
    return 1;
}
