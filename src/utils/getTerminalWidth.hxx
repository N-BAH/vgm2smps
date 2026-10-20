#pragma once

constexpr unsigned DEFAULT_TERMINAL_WIDTH{80U};

auto getTerminalWidth(unsigned defaultWidth = DEFAULT_TERMINAL_WIDTH)
    -> unsigned;