typedef unsigned long long u64;
typedef long long s64;

static u64 udivmod64(u64 n, u64 d, u64 *rem)
{
    u64 q = 0, r = 0;
    if (d == 0) {
        if (rem) *rem = 0;
        return 0;
    }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (u64)1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

u64 __udivdi3(u64 a, u64 b) { return udivmod64(a, b, 0); }
u64 __umoddi3(u64 a, u64 b) { u64 r; udivmod64(a, b, &r); return r; }

s64 __divdi3(s64 a, s64 b)
{
    int neg = (a < 0) ^ (b < 0);
    u64 q = udivmod64(a < 0 ? -(u64)a : (u64)a, b < 0 ? -(u64)b : (u64)b, 0);
    return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 a, s64 b)
{
    u64 r;
    udivmod64(a < 0 ? -(u64)a : (u64)a, b < 0 ? -(u64)b : (u64)b, &r);
    return a < 0 ? -(s64)r : (s64)r;
}

void abort(void) { for (;;) {} }
