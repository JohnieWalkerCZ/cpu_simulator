diagrams=("complete 10000", "classes 3000", "executor 4000", "microop 4000")

for i in "${diagrams[@]}"; 
do
    set -- $i 
    echo "GENERATING: $1.mmd"
    mmdc -i $1.mmd -o $1.png -w $2
    echo "" 
done
