// only compare int streams, ignoring empty spaces and line breaks
#include <stdio.h>

const int ZOJ_WA_CODE = -1;
const int AC_CODE = 0;
int main() {
    FILE *fstd, *fuser;
    fstd = fopen("output", "r");
    fuser = fopen("user_output", "r");

    int std_answer, user_answer;
    while (fscanf(fstd, "%d", &std_answer) == 1) {
        fscanf(fuser, "%d", &user_answer);
        if (std_answer != user_answer) {
            return ZOJ_WA_CODE;
        }
    }
    return AC_CODE;
}
