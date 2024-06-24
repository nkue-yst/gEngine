#pragma once

#include <gngin_type.h>

namespace gngin
{
    class GnginEngine;

    /**
     * @brief エンジンを明示的に初期化
     */
    GN_BOOL initializeEngine();

    /**
     * @brief エンジンを明示的に終了
     */
    void finalizeEngine();

    /**
     * @brief エンジンが初期化されているかどうかを取得
     */
    GN_BOOL isEngineInitialized();
}
