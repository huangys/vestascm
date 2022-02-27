
for i in ../mtex/* ; do
    echo $i
    j=`basename $i .mtex`
    echo $j
    cat $i | ../src/linux/lim ../bin/mtex2man.lim > $j
done;
