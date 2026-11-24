#pragma once

template <typename... Calls>
struct Overloaded : Calls... {
    using Calls::operator()...;
};
