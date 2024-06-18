#pragma once

namespace gngin
{
    class GnginEngine
    {
    private:
        inline static GnginEngine *instance = nullptr;

        /**
         * @brief コンストラクタ（直接呼び出し不可）
         */
        GnginEngine() = default;

        /**
         * @brief デストラクタ（直接呼び出し不可）
         */
        ~GnginEngine() = default;

    public:
        /**
         * @brief インスタンスを取得する
         * @return インスタンス
         */
        static GnginEngine *getInstance()
        {
            if (GnginEngine::instance == nullptr)
            {
                GnginEngine::instance = new GnginEngine();
            }

            return GnginEngine::instance;
        }

        /**
         * @brief エンジンを初期化する
         */
        bool initialize()
        {
            GnginEngine::getInstance();

            return (GnginEngine::instance != nullptr);
        }

        /**
         * @brief エンジンを終了する
         */
        void finalize()
        {
            if (GnginEngine::instance != nullptr)
            {
                delete GnginEngine::instance;
                GnginEngine::instance = nullptr;
            }
        }
    };
}
