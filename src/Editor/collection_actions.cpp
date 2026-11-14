#include "vm_actions.hpp"
#include <algorithm>
#include <array>
#include <charconv>

namespace gengine::editor
{
    namespace
    {
        bool ValidIdentifier(std::string_view name)
        {
            return name.empty() == false && name.size() <= 128 &&
                   name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") ==
                       std::string_view::npos;
        }

        ActionError Failure(GvmStatus status, const GvmDiagnostic& diagnostic, std::string field)
        {
            const auto* code = status == GVM_BUSY ? "Busy" : status == GVM_INVALID_STATE ? "InvalidState" : "VmError";
            return {code, std::move(field), diagnostic.present != 0 ? diagnostic.code : "VM operation failed"};
        }

        // 正常終了・途中のreturn・例外のいずれでも、借用を関数の外へ持ち出さない。
        struct ViewLease
        {
            Gvm* vm;
            GvmPoolView view;
            ~ViewLease()
            {
                if (gvm_release_view(vm, view, nullptr) != GVM_OK) {
                    std::terminate();
                }
            }
        };

        template <class T> std::string NumberText(T value)
        {
            auto buffer = std::array<char, 64>{};
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            if (result.ec != std::errc{}) {
                throw std::runtime_error("cannot format pool value");
            }
            return {buffer.data(), result.ptr};
        }

        std::string CellText(const GvmColumn& column, std::uint32_t index)
        {
            switch (column.type) {
            case GVM_BOOL:
                return static_cast<const std::uint8_t*>(column.data)[index] != 0 ? "true" : "false";
            case GVM_I32:
                return NumberText(static_cast<const std::int32_t*>(column.data)[index]);
            case GVM_U32:
                return NumberText(static_cast<const std::uint32_t*>(column.data)[index]);
            case GVM_F32:
                return NumberText(static_cast<const float*>(column.data)[index]);
            default:
                throw std::runtime_error("unsupported pool column type");
            }
        }
    } // namespace

    std::optional<ActionError> VmActions::CheckCall(const VmCallRequest& request) const
    {
        if (const auto error = CheckVm({request.vm_id}); error.has_value()) {
            return error;
        }

        if (ValidIdentifier(request.function) == false) {
            return ActionError{"InvalidArgument", "function",
                               "use an export name of 1 to 128 ASCII identifier characters"};
        }

        if (request.fuel.has_value() && (*request.fuel == 0 || *request.fuel > kMaxCallFuel)) {
            return ActionError{"InvalidArgument", "fuel", "fuel must be in 1..1000000"};
        }

        // Faultedの診断は保持したまま、利用者がresetしてから次の関数を実行する。
        auto state = std::uint32_t{};
        gvm_get_state(vms_.at(request.vm_id).vm, &state);
        if (state != GVM_READY) {
            return ActionError{"InvalidState", "vm_id", "reset the Faulted VM before calling an export"};
        }
        return {};
    }

    Result<ResetResult> VmActions::Call(const VmCallRequest& request)
    {
        // 署名の照合をVMへ委ね、不一致なら実行前に失敗させる。
        auto* vm = vms_.at(request.vm_id).vm;
        auto function = GvmExport{};
        auto diagnostic = GvmDiagnostic{};
        auto status = gvm_resolve_export(vm, request.function.data(), request.function.size(), nullptr, 0, GVM_VOID,
                                         &function, &diagnostic);
        if (status != GVM_OK) {
            return std::unexpected(Failure(status, diagnostic, "function"));
        }

        // VM実行だけを測る。失敗時も予算と時間を残し、診断は既存の取得操作で読む。
        const auto fuel = request.fuel.value_or(kMaxCallFuel);
        auto result = GvmValue{};
        const auto start = std::chrono::steady_clock::now();
        status = gvm_call(vm, function, nullptr, 0, fuel, &result, &diagnostic);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        auto stats = GvmStats{};
        if (gvm_get_stats(vm, &stats) == GVM_OK) {
            vms_.at(request.vm_id).last_call = CallMetrics{
                fuel,
                fuel - stats.fuel_remaining,
                stats.fuel_remaining,
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
                status == GVM_OK,
                request.function};
        }
        if (status != GVM_OK) {
            return std::unexpected(Failure(status, diagnostic, "function"));
        }
        return ResetResult{request.vm_id, "Ready"};
    }

    std::optional<ActionError> VmActions::CheckPool(const PoolRequest& request) const
    {
        if (const auto error = CheckVm({request.vm_id}); error.has_value()) {
            return error;
        }

        auto state = std::uint32_t{};
        gvm_get_state(vms_.at(request.vm_id).vm, &state);
        if (state != GVM_READY) {
            return ActionError{"InvalidState", "vm_id", "reset the Faulted VM before inspecting a pool"};
        }

        // 大きな集合でも、表示要求一件が持つコピー量を一定範囲に抑える。
        if (ValidIdentifier(request.pool) == false || request.limit == 0 || request.limit > 64 ||
            request.columns.size() > 8) {
            return ActionError{"InvalidArgument", "pool", "pool name, limit (1..64) or column count (0..8) is invalid"};
        }
        for (const auto& column : request.columns) {
            if (ValidIdentifier(column.field) == false || column.component > 2) {
                return ActionError{"InvalidArgument", "columns", "field name or component (0..2) is invalid"};
            }
        }
        return {};
    }

    Result<PoolSnapshot> VmActions::Inspect(const PoolRequest& request)
    {
        const auto start = std::chrono::steady_clock::now();
        auto metrics = InspectionMetrics{};
        auto* vm = vms_.at(request.vm_id).vm;
        auto view = GvmPoolView{};
        auto info = GvmPoolInfo{};
        auto diagnostic = GvmDiagnostic{};
        const auto status = gvm_inspect_pool(vm, request.pool.data(), request.pool.size(), &view, &info, &diagnostic);
        if (status != GVM_OK) {
            return std::unexpected(Failure(status, diagnostic, "pool"));
        }
        const auto lease = ViewLease{vm, view};

        // offsetが末尾以降でも件数・容量を返す。減算前に範囲を確定して桁あふれを防ぐ。
        const auto offset = std::min(request.offset, info.length);
        const auto count = std::min(request.limit, info.length - offset);
        auto snapshot = PoolSnapshot{request.vm_id, request.pool, info.length, info.capacity, offset, count, {}};
        snapshot.columns.reserve(request.columns.size());
        for (const auto& requested : request.columns) {
            auto column = GvmColumn{};
            const auto column_status = gvm_get_pool_column(vm, view, requested.field.data(), requested.field.size(),
                                                           requested.component, &column, &diagnostic);
            if (column_status != GVM_OK) {
                return std::unexpected(Failure(column_status, diagnostic, "columns"));
            }

            // ABIが約束する型・幅を確認し、有効な個体だけを表示用の所有データへコピーする。
            if (column.type > GVM_F32 || column.length != info.length ||
                column.stride != (column.type == GVM_BOOL ? 1u : 4u)) {
                return std::unexpected(ActionError{"VmError", "columns", "unexpected column layout"});
            }
            const auto types = std::array{"bool", "i32", "u32", "f32"};
            auto copied = PoolColumnSnapshot{requested.field, requested.component, types[column.type], {}};
            copied.values.reserve(count);
            metrics.source_value_bytes += static_cast<std::uint64_t>(count) * column.stride;
            for (auto index = offset; index < offset + count; ++index) {
                copied.values.push_back(CellText(column, index));
                metrics.value_text_bytes += copied.values.back().size();
            }
            snapshot.columns.push_back(std::move(copied));
        }

        // vectorの予約領域とstringの容量上界は別々に報告する。操作履歴やJSONの複製は含めない。
        metrics.vector_reserved_bytes = snapshot.columns.capacity() * sizeof(PoolColumnSnapshot);
        metrics.string_capacity_bound_bytes = snapshot.vm_id.capacity() + snapshot.pool.capacity() + 2;
        for (const auto& column : snapshot.columns) {
            metrics.vector_reserved_bytes += column.values.capacity() * sizeof(std::string);
            metrics.string_capacity_bound_bytes += column.field.capacity() + column.type.capacity() + 2;
            for (const auto& value : column.values) {
                metrics.string_capacity_bound_bytes += value.capacity() + 1;
            }
        }
        metrics.elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        metrics.pool = snapshot.pool;
        metrics.offset = snapshot.offset;
        metrics.count = snapshot.count;
        metrics.columns = static_cast<std::uint32_t>(snapshot.columns.size());
        vms_.at(request.vm_id).last_inspection = std::move(metrics);

        // 戻り値には文字列しか残らず、leaseの破棄がGUIへ返す前に借用を解放する。
        return snapshot;
    }
} // namespace gengine::editor
