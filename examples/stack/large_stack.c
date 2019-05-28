#include <stdio.h>
int dfs(int x, int MAX) {
    if (x == MAX) return 1;
    int tmp[10] = {};
    for (int i = 0; i < 10; ++i) {
        tmp[i] = x - i;
    }
    int res = dfs(x+1, MAX), tot = 0;
    for (int i = 0; i < 10; ++i) {
        tot += i * tmp[i] * res;
    }
    return tot;
}
int main() {
    int a, b;
    while(scanf("%d %d",&a, &b) != EOF) {
        printf("%d\n", dfs(0, 1000000));
    }
    return 0;
}