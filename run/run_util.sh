SIMTIME=0.8
OVERRIDE=false
TRY_FIND=true
SUCCESS=true
FILES=()

display_help() {
    echo "Syntax: $0 [options]"
    echo "-h            Display this help message."
    echo "-s simtime    Run simulations for simtime ms (default: 0.8)."
    echo "-o            Force rerun and override of existing complete output files."
    echo "-f            Do not look for existing complete output files from other directories."
}

check_res() {
    if ! grep Util $FNAME | grep -q $SIMTIME_MS; then
        echo "Failed to run $FNAME, file is incomplete!"
        SUCCESS=false
    else
        echo "Successfully finished running $FNAME!"
    fi
}

find_and_copy() {
    if [[ $OVERRIDE = true ]] || [[ $TRY_FIND = false ]]; then
        return 1
    fi
    #try finding same-name file from other Figure directories, if found copy it
    LOCS=$(find ../ -name "$FNAME")
    for LOC in $LOCS; do
        if grep Util $LOC | grep -q $SIMTIME_MS; then
            echo "Found existing and complete $LOC file, copying and skipping..."
            cp $LOC .
            return 0
        fi
    done
    return 1
}

run_sim () {
echo "Checking for ${FNAME}..."
if [[ $OVERRIDE = true ]] || ! [[ -e $FNAME ]] || ! grep Util $FNAME | grep -q $SIMTIME_MS; then
    find_and_copy
    if [[ $? -eq 1 ]]; then
        echo "Running ${FNAME}..."
        eval $COMMAND
        check_res
    fi
else
    echo "Found complete ${FNAME}, skipping."
fi
}

#get options and set all arguments
while getopts "hofs:" option; do
    case $option in
        h)  display_help
            exit;;
        o)  OVERRIDE=true;;
        f)  TRY_FIND=false;;
        s)  SIMTIME=$OPTARG;;
        \?)  echo "Invalid option: ${OPTARG}."
            display_help
            exit;;
    esac
done

SIMTIME_FULL=${SIMTIME}01 #always add slighly more
SIMTIME_MS=$(echo $SIMTIME*1000 | bc) #to check for simtime inside output file
