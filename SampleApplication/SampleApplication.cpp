#include "Main.h"
#include <iostream>

int main(int argc, char** argv)
{
    gngin::Main gn_main;

    // ����m�F�p�ɓ��͑҂�
    int value;
    std::cin >> value;

    return gn_main.Exec(argc, argv);
}
