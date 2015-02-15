#!/bin/bash -e
#
# Scans an executable for runtime dependencies and copies them to the
# executable's directory. This script relies on the mingw-objdump tool to
# parse dependencies. It processes all input recursively.
#
# Copied by permission from:
# https://github.com/jdolan/quake2world/blob/master/mingw-cross/dllbundler.sh
#
# Licensed under the GPLv2

while getopts "h:" opt; do
	case "${opt}" in
		h)
			host="${OPTARG}"
			;;
		\?)
			echo "Invalid option: -${OPTARG}" >&2
			exit 1
			;;
	esac
done

test "${host}" || {
	echo "Required option -h host is missing" >&2
	exit 1
}

objdump=$(which ${host}-objdump)
test -x "${objdump}" || {
	echo "No ${host}-objdump in PATH" >&2
	exit 2
}

shift $((OPTIND-1))

exes="${@}"
for exe in ${exes}; do
	test -e "${exe}" || {
		echo "${exe} is not an executable" >&2
		exit 3
	}

	dir=$(dirname "${exe}")
	test -w "${dir}" || {
		echo "${dir} is not writable" >&2
		exit 3
	}

	# Clean up ${dir} bedore copying .dll files
	pushd ${dir}
	rm -f $(find . -type f | egrep -v "cygwin|*.exe")
	popd
done

tmp=$(mktemp -d /tmp/dllbundler-XXXXXX)
test -w "${tmp}" || {
	echo "${tmp} is not writable" >&2
	exit 4
}

search_path="${MINGW_PREFIX}/usr/${host}"
test -d "${search_path}" || {
	echo "${search_path} does not exist" >&2
	exit 5
}


#
# Resolve dependencies recursively, copying them from the search path to dir.
#
function bundle_recursively(){
	local deps=$($objdump -p "${1}" | sed -rn 's/DLL Name: (.*\.dll)/\1/p' | sort -u)
	for dep in ${deps}; do
		test -f "${dir}/${dep}" && continue
		test -f "${tmp}/${dep}" && continue

		local dll=$(find "${search_path}" -name "${dep}")
		test -z "${dll}" && {
			echo "WARNING: Couldn't find ${dep} in ${search_path}" >&2
			touch "${tmp}/${dep}"
			continue
		}

		bundle_recursively "${dll}"

		echo "Installing ${dll}.."
		install "${dll}" "${dir}"
	done
}

for exe in ${exes}; do
    dir=$(dirname "${exe}")
    echo "Bundling .dll files for ${exe} in ${dir}.."
	bundle_recursively "${exe}"
done

rm -rf "${tmp}"
