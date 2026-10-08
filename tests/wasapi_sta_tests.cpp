// SPDX-License-Identifier: GPL-3.0-only
// Native Windows control test; discovery creates no streams or recording jobs.
#include <soundcurrent/wasapi_capture.hpp>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
int main() {
    try {
        const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) throw std::runtime_error("Cannot initialize fixture STA");
        struct Release { ~Release() { CoUninitialize(); } } release;
        APTTYPE before{}, after{};
        APTTYPEQUALIFIER qualifier{};
        if (FAILED(CoGetApartmentType(&before, &qualifier)) ||
            (before != APTTYPE_STA && before != APTTYPE_MAINSTA))
            throw std::runtime_error("Fixture is not in STA");
        std::size_t count = 0;
        for (unsigned n = 0; n < 3; ++n) {
            count = wasapiEndpoints().size();
            wasapiDefaultEndpoints();
            if (FAILED(CoGetApartmentType(&after, &qualifier)) || after != before)
                throw std::runtime_error("Discovery released/changed caller COM apartment");
        }
        std::cout << "STA discovery passed; repeated inventories=3 endpoints=" << count
                  << " caller apartment retained\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
