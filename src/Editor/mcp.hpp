#pragma once
#include "editor_core.hpp"
#include "json.hpp"
#include <atomic>
#include <condition_variable>

namespace gengine::editor
{
    // JSON-RPCの処理も所有スレッドで行い、GUIと同じDispatcherを使う。
    class McpAdapter
    {
      public:
        explicit McpAdapter(EditorCore& core) : core_(core)
        {
        }

        // 所有スレッドから受信済みの要求を渡し、送信するJSONの一覧を受け取る。
        std::vector<std::string> Receive(const std::vector<std::string>& messages);

      private:
        std::optional<Json> Handle(const Json& message);
        Json ToolList() const;
        Json CallTool(const Json& parameters);

        EditorCore& core_;
        bool initialized_ = false;
        bool ready_ = false;
    };

    // ブロックするstdio入出力をGUIから分離する。両キューは件数と容量を制限する。
    class StdioTransport
    {
      public:
        StdioTransport();
        ~StdioTransport();

        // 所有スレッドとの受渡し。通信スレッドからEditorの状態は変更しない。
        std::vector<std::string> Take();
        void Send(std::string message);

        bool Disconnected() const
        {
            return disconnected_.load();
        }

        // 待機中のI/Oも取り消し、両スレッドが終了するまで回収する。
        void Stop();

      private:
        void ReadLoop();
        void WriteLoop();

        std::atomic<bool> stopping_ = false;
        std::atomic<bool> disconnected_ = false;

        // キューと容量のカウンターは、同じmutexで保護する。
        std::mutex mutex_;
        std::condition_variable changed_;
        std::deque<std::string> incoming_;
        std::deque<std::string> outgoing_;
        std::size_t incoming_bytes_ = 0;
        std::size_t outgoing_bytes_ = 0;

        std::thread reader_;
        std::thread writer_;
    };
} // namespace gengine::editor
