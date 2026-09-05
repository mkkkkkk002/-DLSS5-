#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

// The snippet drives the parameter block through a vtable, never through exported helpers.
// NVSDK_NGX_Parameter declares eight Set overloads then eight Get overloads, in this order:
//   ULL, float, double, uint, int, ID3D11Resource*, ID3D12Resource*, void*
// Getters mirror the setters eight slots up and return 1 on success, 0 when the name is absent.
//
// We build this block ourselves instead of borrowing the driver core's. That matters for two
// reasons: the core's block puts floats somewhere other than the header's slot 1 (OptiScaler has
// to probe for it, and reports finding it at 6), and reaching the core's block at all in a
// standalone process means standing up NGX without a game around it. Owning the block means the
// float slot is whatever we say it is and there is nothing to probe.

struct NgxValue {
    enum class Kind { Ull, Flt, Dbl, Uint, Int, Ptr };
    Kind kind = Kind::Ull;
    union {
        uint64_t ull;
        float f;
        double d;
        uint32_t u;
        int32_t i;
        void* p;
    } v{};
};

class NgxParams {
public:
    NgxParams();

    // The pointer to hand to NGX. The vtable pointer is the first member, so the object
    // address doubles as the block handle.
    void* ptr() noexcept { return this; }
    const void* ptr() const noexcept { return this; }

    // Typed accessors for our own use, mirroring what the snippet will go through the vtable for.
    void setULL(const char* name, uint64_t v);
    void setFloat(const char* name, float v);
    void setDouble(const char* name, double v);
    void setUInt(const char* name, uint32_t v);
    void setInt(const char* name, int v);
    void setPtr(const char* name, void* v);

    bool getULL(const char* name, uint64_t& out) const;
    bool getFloat(const char* name, float& out) const;
    bool getUInt(const char* name, uint32_t& out) const;
    bool getPtr(const char* name, void*& out) const;

    bool has(const char* name) const;
    size_t size() const noexcept { return m_store.size(); }
    void clear() noexcept { m_store.clear(); }

private:
    // MUST stay the first data member: the snippet resolves the vtable from *(void**)block.
    void* m_vtable;
    std::unordered_map<std::string, NgxValue> m_store;

    // Setters occupy slots 0..7, getters 8..15.
    static void slotSetULL(NgxParams* self, const char* name, uint64_t v);
    static void slotSetFloat(NgxParams* self, const char* name, float v);
    static void slotSetDouble(NgxParams* self, const char* name, double v);
    static void slotSetUInt(NgxParams* self, const char* name, uint32_t v);
    static void slotSetInt(NgxParams* self, const char* name, int v);
    static void slotSetD3D11(NgxParams* self, const char* name, void* v);
    static void slotSetD3D12(NgxParams* self, const char* name, void* v);
    static void slotSetVoid(NgxParams* self, const char* name, void* v);

    static int slotGetULL(NgxParams* self, const char* name, uint64_t* out);
    static int slotGetFloat(NgxParams* self, const char* name, float* out);
    static int slotGetDouble(NgxParams* self, const char* name, double* out);
    static int slotGetUInt(NgxParams* self, const char* name, uint32_t* out);
    static int slotGetInt(NgxParams* self, const char* name, int* out);
    static int slotGetD3D11(NgxParams* self, const char* name, void** out);
    static int slotGetD3D12(NgxParams* self, const char* name, void** out);
    static int slotGetVoid(NgxParams* self, const char* name, void** out);
};
