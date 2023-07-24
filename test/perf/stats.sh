#!/usr/bin/env bash

BIN=${1}
N_ELEM=${2:-1000000}
N_CORE=${3:-$(($(nproc) - 1))}
N_RUN=${4:-10}

usage() {
(
    name=$(basename "$0")
    len=$(printf "%s" "$name" |wc -m)
    ___O=$(printf "%*s" "$len" ' ')

    cat <<EOF
Usage: $name <binary> [nb-elem [nb-thread [nb-run]]]

Run <binary> <nb-run> times and aggregate statistics.

       $___O binary: Performance test binary path.
       $___O nb-elem: Number of element to insert.
       $___O nb-thread: Number of thread to run with.
       $___O nb-run: Number run to do in total.
EOF
)
    exit 0;
}

case "$*" in
    *-h*|*--help*|*-help*) usage;;
esac

[ "$BIN" ] || usage;

BIN="$(readlink -f "$BIN")"
[ "$BIN" ] || {
    echo "Binary '$1' not found."
    exit 1
}

CMD="$BIN -n $N_ELEM -c $N_CORE"
CMD_fast="$BIN -n 1 -c 1"

$CMD_fast 2>&1 > /dev/null || {
    echo "Provided binary does not conform to the test parameters."
    exit 1
}

xc() { python3 -c "import math; print(float($*))"; }
join_by() {( IFS="$1"; shift; echo "$*"; )}

stdev() {(
    m=$1; shift;
    sum=0
    for v in $*; do
        sum=$(xc "$sum + pow($v - $m, 2)")
    done
    echo $(xc "math.sqrt($sum / $#)")
)}
mean() { echo $(xc "($(join_by + $*)) / $#"); }

# $1: name field max width
# $2: stat name
# $3-: values
print_stats() {(
    len=$1; shift;
    name=$1; shift;
    values="$*"
    m=$(mean $values)
    sd=$(stdev $m $values)
    printf "%*s: avg %6.1f | stdev %6.1f | max %5.0f | min %5.0f\n" \
        "$len" "$name" "$m" "$sd" \
        "$(xc "max($(join_by , $values),0)")" \
        "$(xc "min($(join_by , $values),0xffffffff)")"
)}

tmp=$(mktemp)
$CMD_fast | grep -v 'Benchmarking\|Reader' | \
while read line; do
    name="$(echo $line | cut -d: -f1)"
    printf "%s reader\t%s writers\t" "$name" "$name"
done >> $tmp
printf "\n" >> $tmp

echo "$N_RUN times '$CMD':"

for i in $(seq 1 $N_RUN); do
$CMD | grep -v 'Benchmarking\|Reader' | \
while read line; do
    reader="$(echo $line  | cut -d: -f2- | cut -d' ' -f2)"
    writers="$(echo $line | cut -d: -f2- | rev | cut -d' ' -f2 | rev)"
    printf "%d\t%d\t" $reader $writers
done >> $tmp
printf "\n" >> $tmp
done

#cat $tmp

TAB="$(printf '%b_' '\t')"; TAB="${TAB%_}"
nb_col=$(awk "-F${TAB}" '{print NF-1}' $tmp | head -1)
maxlen=0
for i in $(seq 1 $nb_col); do
    name=$(head -1 $tmp | cut -d$'\t' -f$i)
    len=$(expr $(printf "%s" "$name" |wc -m))
    [ "$maxlen" -lt "$len" ] && maxlen=$len
done

for i in $(seq 1 $nb_col); do
    name=$(head -1 $tmp | cut -d$'\t' -f$i)
    values=$(tail -n +2 $tmp | cut -d$'\t' -f$i)
    print_stats $maxlen "$name" $values
done

rm $tmp
