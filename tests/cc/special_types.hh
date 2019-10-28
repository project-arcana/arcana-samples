#pragma once

struct regular_type
{
};

struct move_only_type
{
    move_only_type(move_only_type const&) = delete;
    move_only_type(move_only_type&&) = default;
    move_only_type& operator=(move_only_type const&) = delete;
    move_only_type& operator=(move_only_type&&) = default;
};

struct no_default_type
{
    no_default_type() = delete;
    explicit no_default_type(int) {}
};
