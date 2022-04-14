#include <nexus/test.hh>

#include <rich-log/log.hh>

#include <iostream>

#include <clean-core/vector.hh>

namespace
{
// T is field
template <class T>
struct matrix
{
    matrix() = default;

    // zero mat
    matrix(int rows, int cols) : _rows(rows), _cols(cols) { _values.resize(rows * cols); }

    int idx_of(int r, int c) const { return r * _cols + c; }

    T& at(int r, int c) { return operator()(r, c); }
    T const& at(int r, int c) const { return operator()(r, c); }
    T& operator()(int r, int c)
    {
        CC_ASSERT(0 <= r && r < _rows);
        CC_ASSERT(0 <= c && c < _cols);
        return _values[idx_of(r, c)];
    }
    T const& operator()(int r, int c) const
    {
        CC_ASSERT(0 <= r && r < _rows);
        CC_ASSERT(0 <= c && c < _cols);
        return _values[idx_of(r, c)];
    }

    int row_count() const { return _rows; }
    int col_count() const { return _cols; }

    void print()
    {
        for (auto r = 0; r < _rows; ++r)
        {
            for (auto c = 0; c < _cols; ++c)
                std::cout << at(r, c) << ' ';
            std::cout << std::endl;
        }
    }

    bool is_zero() const
    {
        for (auto const& v : _values)
            if (v != T{})
                return false;
        return true;
    }

    bool is_col_vector() const { return _cols == 1; }
    bool is_row_vector() const { return _rows == 1; }

    void set_col(int c, matrix<T> const& col)
    {
        CC_ASSERT(col.row_count() == row_count());
        CC_ASSERT(col.is_col_vector());
        for (auto r = 0; r < _rows; ++r)
            at(r, c) = col(r, 0);
    }

    matrix transposed() const
    {
        auto A = matrix(_cols, _rows);
        for (auto r = 0; r < _rows; ++r)
            for (auto c = 0; c < _cols; ++c)
                A(c, r) = at(r, c);
        return A;
    }

    void swap_rows(int r0, int r1)
    {
        for (auto c = 0; c < _cols; ++c)
            cc::swap(at(r0, c), at(r1, c));
    }

    void add_row_mult(int r_to, int r_from, T f)
    {
        for (auto c = 0; c < _cols; ++c)
            at(r_to, c) = at(r_to, c) + at(r_from, c) * f;
    }

    void apply_gauss(int r0 = 0, int c0 = 0)
    {
        if (r0 >= _rows || c0 >= _cols)
            return; // done

        // zero pivot
        if (at(r0, c0) == T{})
        {
            // find new row
            for (auto r1 = r0 + 1; r1 < _rows; ++r1)
            {
                if (at(r1, c0) != T{})
                {
                    swap_rows(r0, r1);
                    apply_gauss(r0, c0);
                    return;
                }
            }

            // no non-zero?
            apply_gauss(r0, c0 + 1);
            return;
        }

        // sub from next
        auto pivot_inv = at(r0, c0).mult_inverse();
        for (auto r1 = r0 + 1; r1 < _rows; ++r1)
            add_row_mult(r1, r0, -at(r1, c0) * pivot_inv);

        apply_gauss(r0 + 1, c0 + 1);
    }

private:
    int _rows = 0;
    int _cols = 0;
    cc::vector<T> _values;
};

template <class T>
matrix<T> matrix_from_cols(cc::span<matrix<T> const> cols)
{
    CC_ASSERT(!cols.empty());
    auto A = matrix<T>(cols[0].row_count(), cols.size());
    for (auto c = 0; c < int(cols.size()); ++c)
    {
        auto const& col = cols[c];
        CC_ASSERT(col.row_count() == A.row_count());
        for (auto r = 0; r < col.row_count(); ++r)
            A(r, c) = col(r, 0);
    }
    return A;
}

template <class T>
bool is_basis(cc::span<matrix<T> const> cols)
{
    CC_ASSERT(int(cols.size()) == cols[0].row_count());
    auto A = matrix_from_cols(cols);
    A.apply_gauss();
    for (auto i = 0; i < A.row_count(); ++i)
        if (A(i, i) == T{})
            return false;
    return true;
}

template <class T>
bool could_be_basis(cc::span<matrix<T> const> cols)
{
    CC_ASSERT(int(cols.size()) <= cols[0].row_count());
    auto A = matrix_from_cols(cols);
    A.apply_gauss();
    for (auto i = 0; i < A.col_count(); ++i)
        if (A(i, i) == T{})
            return false;
    return true;
}

template <class T>
matrix<T> to_vector(cc::span<T const> vals)
{
    auto A = matrix<T>(vals.size(), 1);
    for (auto i = 0; i < int(vals.size()); ++i)
        A(i, 0) = vals[i];
    return A;
}

template <int N>
struct Zn
{
    Zn() = default;
    explicit Zn(int n) { _val = (n % N + N) % N; }

    explicit operator int() const { return _val; }

    Zn operator+(Zn rhs) const { return Zn(_val + rhs._val); }
    Zn operator-(Zn rhs) const { return Zn(_val - rhs._val); }
    Zn operator*(Zn rhs) const { return Zn(_val * rhs._val); }
    Zn operator-() const { return Zn(-_val); }
    bool operator==(Zn rhs) const { return _val == rhs._val; }
    bool operator!=(Zn rhs) const { return _val != rhs._val; }

    friend std::ostream& operator<<(std::ostream& out, Zn z)
    {
        out << z._val;
        return out;
    }

    Zn mult_inverse() const
    {
        CC_ASSERT(_val != 0);
        for (auto i = 1; i < N; ++i)
            if (i * _val % N == 1)
                return Zn(i);

        CC_BUILTIN_UNREACHABLE;
    }

private:
    int _val = 0;
};
using Z3 = Zn<3>;
using Z5 = Zn<5>;

template <int N>
cc::vector<Zn<N>> all_values_of_Zn()
{
    auto v = cc::vector<Zn<N>>::defaulted(N);
    for (auto i = 0; i < N; ++i)
        v[i] = Zn<N>(i);
    return v;
}

template <int N, class... Args>
matrix<Zn<N>> make_Zn_vector(Args... args)
{
    auto v = matrix<Zn<N>>(sizeof...(args), 1);
    int i = 0;
    ((v(i++, 0) = Zn<N>(args)), ...);
    return v;
}

template <int N>
cc::vector<matrix<Zn<N>>> all_vectors_of_Zn(int D)
{
    if (D <= 0)
        return {matrix<Zn<N>>(0, 0)};

    auto prev = all_vectors_of_Zn<N>(D - 1);
    auto vals = all_values_of_Zn<N>();
    cc::vector<matrix<Zn<N>>> res;
    for (auto const& vec : prev)
        for (auto const& val : vals)
        {
            auto& v = res.emplace_back(D, 1);
            for (auto i = 0; i < D - 1; ++i)
                v(i, 0) = vec(i, 0);
            v(D - 1, 0) = val;
        }
    return res;
}
}

TEST("LA stuff", disabled)
{
    // for (auto const& v : all_vectors_of_Zn<3>(3))
    //     if (!v.is_zero())
    //         v.transposed().print();

    constexpr int N = 2;
    auto all_vecs = all_vectors_of_Zn<N>(4);

    LOG("{} vecs", all_vecs.size());

    cc::vector<matrix<Zn<N>>> cols;
    cols.resize(5);
    auto col_span = cc::span<matrix<Zn<N>> const>(cols);
    auto cnt = 0;
    for (auto const& v0 : all_vecs)
    {
        if (v0.is_zero())
            continue;

        cols[0] = v0;

        for (auto const& v1 : all_vecs)
        {
            if (v1.is_zero())
                continue;

            cols[1] = v1;
            if (!could_be_basis(col_span.subspan(0, 2)))
                continue;

            for (auto const& v2 : all_vecs)
            {
                if (v2.is_zero())
                    continue;

                cols[2] = v2;
                if (!could_be_basis(col_span.subspan(0, 3)))
                    continue;

                for (auto const& v3 : all_vecs)
                {
                    if (v3.is_zero())
                        continue;

                    cols[3] = v3;
                    if (!could_be_basis(col_span.subspan(0, 4)))
                        continue;

                    if (cnt % 100000 == 0)
                        LOG(".. {}", cnt);
                    cnt++;

                    // for (auto const& v4 : all_vecs)
                    // {
                    //     if (v4.is_zero())
                    //         continue;
                    //
                    //     cols[4] = v4;
                    //
                    //     if (is_basis(col_span))
                    //     {
                    //         if (cnt % 1000 == 0)
                    //             LOG(".. {}", cnt);
                    //         cnt++;
                    //     }
                    // }
                }
            }
        }
    }
    LOG("{} bases", cnt);

    // auto A = matrix<Zn<N>>(3, 3);
    // A.print();
    // A.set_col(0, make_Zn_vector<N>(1, 2, 0));
    // A.set_col(1, make_Zn_vector<N>(2, 2, 1));
    // A.set_col(2, make_Zn_vector<N>(0, 2, 1));
    // std::cout << std::endl;
    // A.print();
    // std::cout << std::endl;
    // A.apply_gauss();
    // A.print();
    //
}
