# Narrow pactl text parser (LC_ALL=C). Never emit names, arbitrary properties,
# client identities, cookies, paths or data from other applications.
function emit() {
    if (pid == target && index_value != "") {
        matches++
        printf "GAME_STREAM index=%s sink=%s pid=%s corked=%s mute=%s buffer_usec=%s sink_usec=%s\n", index_value,sink,pid,corked,mute,buffer,latency
    }
}
function reset() { index_value="";sink="unknown";pid="";corked="unknown";mute="unknown";buffer="unknown";latency="unknown" }
BEGIN {reset();matches=0;total=0;rc="unknown"}
/^Sink Input #[0-9]+$/ {emit();reset();index_value=$3;sub(/^#/,"",index_value);total++;next}
/^[ \t]*Sink: [0-9]+$/ {sink=$2;next}
/^[ \t]*Corked: (yes|no)$/ {corked=$2;next}
/^[ \t]*Mute: (yes|no)$/ {mute=$2;next}
/^[ \t]*Buffer Latency: [0-9]+ usec$/ {buffer=$3;next}
/^[ \t]*Sink Latency: [0-9]+ usec$/ {latency=$3;next}
/^[ \t]*application.process.id = "[0-9]+"$/ {pid=$3;gsub(/"/,"",pid);next}
/^QUERY_EXIT=[0-9]+$/ {rc=$0;sub(/^QUERY_EXIT=/,"",rc);next}
END {emit();printf "SINK_INPUTS_TOTAL=%d\nGAME_STREAMS_MATCHING_PID=%d\nQUERY_EXIT=%s\n",total,matches,rc}
