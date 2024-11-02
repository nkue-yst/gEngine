#include "Main.h"

#include <iostream>

int main(int argc, char** argv)
{
    gngin::Main gn_main;

    // 動作確認用に入力待ち
    int value;
    std::cin >> value;

    return gn_main.Exec(argc, argv);
}
