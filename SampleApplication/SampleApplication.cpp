#include "Core/Application/Application.h"

int main(int argc, char** argv)
{
    gngin::Application* app = new gngin::Application(argc, argv);
    
    return app->Exec();
}
