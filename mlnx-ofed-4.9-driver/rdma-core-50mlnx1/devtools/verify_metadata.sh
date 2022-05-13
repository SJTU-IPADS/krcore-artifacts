#!/bin/bash
#
# Copyright (c) 2016 Mellanox Technologies. All rights reserved.
#
# This Software is licensed under one of the following licenses:
#
# 1) under the terms of the "Common Public License 1.0" a copy of which is
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/cpl.php.
#
# 2) under the terms of the "The BSD License" a copy of which is
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/bsd-license.php.
#
# 3) under the terms of the "GNU General Public License (GPL) Version 2" a
#    copy of which is available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/gpl-license.php.
#
# Licensee has the right to choose one of the above licenses.
#
# Redistributions of source code must retain the above copyright
# notice and one of the license notices.
#
# Redistributions in binary form must reproduce both the above copyright
# notice, one of the license notices in the documentation
# and/or other materials provided with the distribution.
#
#
# Author: Alaa Hleihel - alaa@mellanox.com
#
#########################################################################

WDIR=$(cd `dirname "${BASH_SOURCE[0]}"` && pwd | sed -e 's/devtools//')
ORIG_ARGS=$@
path=

FEATURES_DB="metadata/features_metadata_db.csv"
STATUS_DB="NA \
		   ignore \
		   in_progress \
		   sent
		   accepted \
		   rejected \
"

usage()
{
	cat <<EOF
Usage:
	${0##*/} [options]

Options:
	-p, --path <PATH>          Path to the metadata file to test
EOF
}

while [ ! -z "$1" ]
do
	case "$1" in
		-p | --path)
		path="$2"
		shift
		;;
		-h | *help | *usage)
		echo "This script will verify the content of a metadata file."
		usage
		exit 0
		;;
		*)
		echo "-E- Unsupported option: $1" >&2
		exit 1
		;;
	esac
	shift
done


get_subject()
{
	local cid=$1; shift

	echo $(git log -1 --format="%s" $cid)
}

get_id_from_csv()
{
	local line=$1; shift

	echo $(echo "$line" | sed -r -e 's/.*Change-Id=\s*//' -e 's/;\s*subject=.*//')
}

get_commitID()
{
	local uid=$1; shift

	if (git log --format="%h" -1 $uid >/dev/null 2>&1); then
		echo "$uid"
	else
		echo $(git log --format="%h" -1 --grep="$uid" 2>/dev/null)
	fi
}

get_subject_from_csv()
{
	local line=$1; shift

	echo $(echo "$line" | sed -r -e 's/.*;\s*subject=\s*//' -e 's/;\s*feature.*//')
}

get_feature_from_csv()
{
	local line=$1; shift

	echo $(echo "$line" | sed -r -e 's/.*;\s*feature=\s*//' -e 's/;\s*upstream_status.*//')
}

get_upstream_from_csv()
{
	local line=$1; shift

	echo $(echo "$line" | sed -r -e 's/.*;\s*upstream_status=\s*//' -e 's/;\s*general.*//')
}

##################################################################
#
# main
#
if [ ! -e "$path" ]; then
	echo "-E- File doesn't exist '$path' !" >&2
	echo
	usage
	exit 1
fi

RC=0
echo "Scanning file..."
while read -r line
do
	case "$line" in
		*sep*)
		continue
		;;
	esac
	cerrs=

	uid=$(get_id_from_csv "$line")
	if [ "X$uid" == "X" ]; then
		cerrs="$cerrs\n-E- Missing unique ID!"
		RC=$(( $RC + 1))
		echo -n "At line --> '$line'"
		echo -e "$cerrs"
		continue
	fi
	if [ $(grep -wq -- "$uid" $path | wc -l) -gt 1 ]; then
		cerrs="$cerrs\n-E- unique ID '$uid' apprease twice in given csv file!"
		RC=$(( $RC + 1))
		echo -n "At line --> '$line'"
		echo -e "$cerrs"
		continue

	fi
	cid=$(get_commitID $uid)
	if [ -z "$cid" ]; then
		cerrs="$cerrs\n-E- Failed to get commit ID!"
		RC=$(( $RC + 1))
		echo -n "At line --> '$line'"
		echo -e "$cerrs"
		continue
	fi
	commit_subject=$(get_subject $cid)
	line_subject=$(get_subject_from_csv "$line")
	if [ "X$commit_subject" != "X$line_subject" ]; then
		cerrs="$cerrs\n-E- commit $cid subject is wrong (in csv:'$line_subject' vs. in commit:'$commit_subject') !"
		RC=$(( $RC + 1))
	fi

	feature=$(get_feature_from_csv "$line")
	if [ -z "$feature" ]; then
		cerrs="$cerrs\n-E- missing feature field!"
		RC=$(( $RC + 1))
	elif ! (grep -Ewq -- "name=\s*$feature" $WDIR/$FEATURES_DB); then
		cerrs="$cerrs\n-E- feature '$feature' does not exist in '$FEATURES_DB' !"
		RC=$(( $RC + 1))
	fi

	upstream=$(get_upstream_from_csv "$line")
	if [ -z "$upstream" ]; then
		cerrs="$cerrs\n-E- missing upstream_status field!"
		RC=$(( $RC + 1))
	elif ! (echo -e "$STATUS_DB" | grep -wq -- "$upstream"); then
		cerrs="$cerrs\n-E- invalid upstream_status '$upstream' !"
		RC=$(( $RC + 1))
	fi

	if [ ! -z "$cerrs" ]; then
		echo -n "At line --> '$line'"
		echo -e "$cerrs"
		echo
	fi

done < <(cat $path)


echo "Found $RC issues."
if [ $RC -ne 0 ]; then
	echo "Please fix the above issues by manaully editing '$path'."
	echo "Then run the follwoing command to verify that all is OK:"
	echo "# $0 $ORIG_ARGS"
else
	echo "All passed."
fi
exit $RC
