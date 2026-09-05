#include "ngx_params.h"

// The vtable is shared by every block: it is a fixed table of thunks that only differ in the
// object they are handed back, so one static array serves all instances.
static void* g_vtable[16] = {nullptr};

NgxParams::NgxParams() : m_vtable(nullptr) {
    if (g_vtable[0] == nullptr) {
        g_vtable[0] = reinterpret_cast<void*>(&NgxParams::slotSetULL);
        g_vtable[1] = reinterpret_cast<void*>(&NgxParams::slotSetFloat);
        g_vtable[2] = reinterpret_cast<void*>(&NgxParams::slotSetDouble);
        g_vtable[3] = reinterpret_cast<void*>(&NgxParams::slotSetUInt);
        g_vtable[4] = reinterpret_cast<void*>(&NgxParams::slotSetInt);
        g_vtable[5] = reinterpret_cast<void*>(&NgxParams::slotSetD3D11);
        g_vtable[6] = reinterpret_cast<void*>(&NgxParams::slotSetD3D12);
        g_vtable[7] = reinterpret_cast<void*>(&NgxParams::slotSetVoid);

        g_vtable[8] = reinterpret_cast<void*>(&NgxParams::slotGetULL);
        g_vtable[9] = reinterpret_cast<void*>(&NgxParams::slotGetFloat);
        g_vtable[10] = reinterpret_cast<void*>(&NgxParams::slotGetDouble);
        g_vtable[11] = reinterpret_cast<void*>(&NgxParams::slotGetUInt);
        g_vtable[12] = reinterpret_cast<void*>(&NgxParams::slotGetInt);
        g_vtable[13] = reinterpret_cast<void*>(&NgxParams::slotGetD3D11);
        g_vtable[14] = reinterpret_cast<void*>(&NgxParams::slotGetD3D12);
        g_vtable[15] = reinterpret_cast<void*>(&NgxParams::slotGetVoid);
    }
    m_vtable = g_vtable;
}

// ---------------------------------------------------------------- setters

void NgxParams::setULL(const char* name, uint64_t v) {
    NgxValue x;
    x.kind = NgxValue::Kind::Ull;
    x.v.ull = v;
    m_store[name] = x;
}

void NgxParams::setFloat(const char* name, float v) {
    NgxValue x;
    x.kind = NgxValue::Kind::Flt;
    x.v.f = v;
    m_store[name] = x;
}

void NgxParams::setDouble(const char* name, double v) {
    NgxValue x;
    x.kind = NgxValue::Kind::Dbl;
    x.v.d = v;
    m_store[name] = x;
}

void NgxParams::setUInt(const char* name, uint32_t v) {
    NgxValue x;
    x.kind = NgxValue::Kind::Uint;
    x.v.u = v;
    m_store[name] = x;
}

void NgxParams::setInt(const char* name, int v) {
    NgxValue x;
    x.kind = NgxValue::Kind::Int;
    x.v.i = v;
    m_store[name] = x;
}

void NgxParams::setPtr(const char* name, void* v) {
    NgxValue x;
    x.kind = NgxValue::Kind::Ptr;
    x.v.p = v;
    m_store[name] = x;
}

// ---------------------------------------------------------------- getters

bool NgxParams::has(const char* name) const {
    return m_store.find(name) != m_store.end();
}

bool NgxParams::getULL(const char* name, uint64_t& out) const {
    auto it = m_store.find(name);
    if (it == m_store.end()) return false;
    out = it->second.v.ull;
    return true;
}

bool NgxParams::getFloat(const char* name, float& out) const {
    auto it = m_store.find(name);
    if (it == m_store.end()) return false;
    out = it->second.v.f;
    return true;
}

bool NgxParams::getUInt(const char* name, uint32_t& out) const {
    auto it = m_store.find(name);
    if (it == m_store.end()) return false;
    out = it->second.v.u;
    return true;
}

bool NgxParams::getPtr(const char* name, void*& out) const {
    auto it = m_store.find(name);
    if (it == m_store.end()) return false;
    out = it->second.v.p;
    return true;
}

// ---------------------------------------------------------------- vtable thunks
//
// Resources arrive through the 64-bit setter (slot 0) rather than the typed D3D12 setter
// (slot 6): OptiScaler found that writing them through slot 6 left them unset. Both are
// accepted here and land in the same map, so it does not matter which one a caller picks.

void NgxParams::slotSetULL(NgxParams* self, const char* name, uint64_t v) {
    if (!self || !name) return;
    self->setULL(name, v);
}

void NgxParams::slotSetFloat(NgxParams* self, const char* name, float v) {
    if (!self || !name) return;
    self->setFloat(name, v);
}

void NgxParams::slotSetDouble(NgxParams* self, const char* name, double v) {
    if (!self || !name) return;
    self->setDouble(name, v);
}

void NgxParams::slotSetUInt(NgxParams* self, const char* name, uint32_t v) {
    if (!self || !name) return;
    self->setUInt(name, v);
}

void NgxParams::slotSetInt(NgxParams* self, const char* name, int v) {
    if (!self || !name) return;
    self->setInt(name, v);
}

void NgxParams::slotSetD3D11(NgxParams* self, const char* name, void* v) {
    if (!self || !name) return;
    self->setPtr(name, v);
}

void NgxParams::slotSetD3D12(NgxParams* self, const char* name, void* v) {
    if (!self || !name) return;
    self->setPtr(name, v);
}

void NgxParams::slotSetVoid(NgxParams* self, const char* name, void* v) {
    if (!self || !name) return;
    self->setPtr(name, v);
}

int NgxParams::slotGetULL(NgxParams* self, const char* name, uint64_t* out) {
    if (!self || !name || !out) return 0;
    return self->getULL(name, *out) ? 1 : 0;
}

int NgxParams::slotGetFloat(NgxParams* self, const char* name, float* out) {
    if (!self || !name || !out) return 0;
    return self->getFloat(name, *out) ? 1 : 0;
}

int NgxParams::slotGetDouble(NgxParams* self, const char* name, double* out) {
    if (!self || !name || !out) return 0;
    auto it = self->m_store.find(name);
    if (it == self->m_store.end()) return 0;
    *out = it->second.v.d;
    return 1;
}

int NgxParams::slotGetUInt(NgxParams* self, const char* name, uint32_t* out) {
    if (!self || !name || !out) return 0;
    return self->getUInt(name, *out) ? 1 : 0;
}

int NgxParams::slotGetInt(NgxParams* self, const char* name, int* out) {
    if (!self || !name || !out) return 0;
    auto it = self->m_store.find(name);
    if (it == self->m_store.end()) return 0;
    *out = it->second.v.i;
    return 1;
}

int NgxParams::slotGetD3D11(NgxParams* self, const char* name, void** out) {
    if (!self || !name || !out) return 0;
    return self->getPtr(name, *out) ? 1 : 0;
}

int NgxParams::slotGetD3D12(NgxParams* self, const char* name, void** out) {
    if (!self || !name || !out) return 0;
    return self->getPtr(name, *out) ? 1 : 0;
}

int NgxParams::slotGetVoid(NgxParams* self, const char* name, void** out) {
    if (!self || !name || !out) return 0;
    return self->getPtr(name, *out) ? 1 : 0;
}
