#include <stdio.h>

/*
 * 256-color cube: indices 16-231, formula 16 + 36*r + 6*g + b, r/g/b in [0,5].
 *
 * Two interpretations of what each step [0-5] maps to in 8-bit:
 *   Linear   : step * 51          -> 0, 51, 102, 153, 204, 255
 *   xterm std: 0 if step==0,
 *              else 55 + step*40  -> 0, 95, 135, 175, 215, 255
 *
 * Each row displays:
 *   index  r g b  | linear RGB  | xterm RGB  | [256-color] [24-bit linear] [24-bit xterm]
 */

static int linear(int v) { return v * 51; }
static int xterm(int v)  { return v == 0 ? 0 : 55 + v * 40; }

int main(void) {
    printf("Idx   r g b | Linear RGB          | Xterm RGB           "
           "| linear|256|xterm\n");
    printf("------+-----+---------------------+---------------------"
           "+-----+---+-----\n");

    for (int i = 16; i <= 231; i++) {
        int n = i - 16;
        int r = n / 36, g = (n / 6) % 6, b = n % 6;

        int lr = linear(r), lg = linear(g), lb = linear(b);
        int xr = xterm(r),  xg = xterm(g),  xb = xterm(b);

        printf("%3d   %d %d %d | R=%3d G=%3d B=%3d | R=%3d G=%3d B=%3d | "
               "\x1b[48;2;%d;%d;%dm     \x1b[48;5;%dm     \x1b[48;2;%d;%d;%dm     \x1b[0m\n",
               i, r, g, b,
               lr, lg, lb,
               xr, xg, xb,
               lr, lg, lb,
               i,
               xr, xg, xb);
    }

    return 0;
}
