program a
integer :: i, j, stat
do
    read(*,*,iostat=stat) i, j
    if (stat /= 0) exit
    write(*, '(I0)') i+j
end do
end program a
