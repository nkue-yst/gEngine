#pragma once

namespace gngin
{
    class GnginEngine;

    /**
     * @brief エンジンを明示的に初期化
     */
    bool initializeEngine();

    /**
     * @brief エンジンを明示的に終了
     */
    void finalizeEngine();

    /**
     * @brief エンジンが初期化されているかどうかを取得
     */
    bool isEngineInitialized();
}
