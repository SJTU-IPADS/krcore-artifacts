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

base=
num=
dry_run=0
no_edit=0
no_verify=0
ref_db=
changeid_map=
def_feature=
def_ustatus=

usage()
{
	cat <<EOF
Usage:
	${0##*/} [options]

Options:
    -a, --after <BASE>       Add metadata for new commits after given base (commit ID)
    -n, --num <N>            Add metadata for the last N commits in the current branch

    -f, --feature <NAME>            Feature name to assign to new commits.
                                    Must exist in: 'metadata/features_metadata_db.csv'
    -s, --upstream-status <STATUS>  Upstream status to assign to new commits.
                                    Valid values: [NA, ignore, in_progress, sent, accepted, rejected]

    --dry-run                Just print, don't really change anything.

Description for upstream status:
    "NA" -----------> Patch is not applicable for upstream (scripts, Exp. API, etc..).
    "ignore" -------> Patch that should be automatically dropped at next rebase (scripts changes).
    "in_progress" --> Being prepared for Upstream submission.
    "sent" ---------> Sent upstream, but not accepted yet.
    "accepted" -----> Accepted upstream, should be automatically dropped at next rebase.
    "rejected" -----> Sent upstream and got rejected, will be taken again to OFED at next rebase.
EOF
}

while [ ! -z "$1" ]
do
	case "$1" in
		-a | --after)
		base="$2"
		shift
		;;
		-n | --num)
		num="$2"
		shift
		;;
		--dry-run)
		dry_run=1
		;;
		--no-edit)
		no_edit=1
		;;
		--no-verify)
		no_verify=1
		;;
		-r | --ref-db)
		ref_db="$2"
		shift
		;;
		-m | --change-id-map)
		changeid_map="$2"
		shift
		;;
		-f | --feature)
		def_feature="$2"
		shift
		;;
		-s | --upstream-status)
		def_ustatus="$2"
		shift
		;;
		-h | *help | *usage)
		echo "This script will add metadata entries for given commits."
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


is_backports_change_only()
{
	local cid=$1; shift

	tgt=0
	other=0
	for ff in $(git log -1 --name-only --pretty=format: $cid 2>/dev/null)
	do
		if [ -z "$ff" ]; then
			continue
		fi
		case $ff in
			backports* | *compat*)
			tgt=1
			;;
			*)
			other=1
			;;
		esac
	done

	if [ $tgt -eq 1 -a $other -eq 0 ]; then
		return 0
	else
		return 1
	fi
}

is_scripts_change_only()
{
	local cid=$1; shift

	tgt=0
	other=0
	for ff in $(git log -1 --name-only --pretty=format: $cid 2>/dev/null)
	do
		if [ -z "$ff" ]; then
			continue
		fi
		case $ff in
			*ofed_scripts* | *debian* | *devtools*  | *metadata* | *scripts*)
			tgt=1
			;;
			*)
			other=1
			;;
		esac
	done

	if [ $tgt -eq 1 -a $other -eq 0 ]; then
		return 0
	else
		return 1
	fi
}

# get value of given tag if available in the commit message
get_by_tag()
{
	local cid=$1; shift
	local tag=$1; shift

	echo $(git log -1 $cid | grep -iE -- "${tag}\s*:" | head -1 | cut -d":" -f"2" | sed -r -e 's/^\s//g')
}

get_subject()
{
	local cid=$1; shift

	echo $(git log -1 --format="%s" $cid)
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

map_id_new_to_old()
{
	local newid=$1; shift
	local changeid_map=$1; shift
	local subject=$1; shift

	newid=$(echo -e "$newid" | sed -r -e 's/.*=\s*//g')
	local line=$(grep --no-filename -wr -- "$newid" $changeid_map 2>/dev/null)
	local oldid=$(echo "$line" | cut -d':' -f'1')
	if [ "X$oldid" != "X" ]; then
		echo "$oldid"
	else
		local line=$(grep --no-filename -wr -- "$subject" ${ref_db}/*csv 2>/dev/null | tail -1)
		local oldid=$(echo "$line" | cut -d':' -f'1')
		if [ "X$oldid" != "X" ]; then
			echo "$oldid"
		else
			echo "$newid"
		fi
	fi
}

get_feature_from_ref()
{
	local uniqID=$1; shift
	local ref_db=$1; shift
	local subject=$1; shift

	if [ "X$changeid_map" != "X" ]; then
		uniqID=$(map_id_new_to_old $uniqID $changeid_map "$subject")
	fi
	local line=$(grep --no-filename -wr -- "$uniqID" ${ref_db}/*csv 2>/dev/null)
	if [ "X$line" == "X" ]; then
		echo ""
		return
	fi
	get_feature_from_csv "$line"
}

get_upstream_status_from_ref()
{
	local uniqID=$1; shift
	local ref_db=$1; shift
	local subject=$1; shift

	if [ "X$changeid_map" != "X" ]; then
		uniqID=$(map_id_new_to_old $uniqID $changeid_map "$subject")
	fi
	local line=$(grep --no-filename -wr -- "$uniqID" ${ref_db}/*csv 2>/dev/null)
	if [ "X$line" == "X" ]; then
		echo ""
		return
	fi
	local status=$(get_upstream_from_csv "$line")
	if [ "X$status" == "X-1" ]; then
		status=NA
	fi
	echo $status
}

##################################################################
#
# main
#

filter=
if [ "X$base" != "X" ]; then
	filter="${base}.."
fi
if [ "X$num" != "X" ]; then
	filter="-${num}"
fi
if [ "X$filter" == "X" ]; then
	echo "-E- Missing arguments!" >&2
	echo
	usage
	exit 1
fi

if [ "X$ref_db" != "X" ] && ! test -d "$ref_db"; then
	echo "-E- Giving --ref-db does not exist: '$ref_db' !" >&2
	exit 1
fi

commitIDs=$(git log --no-merges --format="%h" $filter | tac)
if [ -z "$commitIDs" ]; then
	echo "-E- Failed to get list of commit IDs." >&2
	exit 1
fi

echo "Getting info about commits..."
echo ----------------------------------------------------
csvfiles=
for cid in $commitIDs
do
	if [ "X$cid" == "X" ]; then
		continue
	fi
	author=$(git log --format="%aN" $cid| head -1 | sed -e 's/ /_/g')
	changeID=
	subject=
	feature=
	upstream=
	general=

	uniqID=
	changeID=$(get_by_tag $cid "change-id")
	if [ -z "$changeID" ]; then
		# for merged commits w/o change ID take the commit ID
		if (git branch -a --contains $cid 2>/dev/null | grep -qEi -- "remote|origin"); then
			uniqID="commit-Id=${cid}"
		else
			echo "-E- Failed to get Change-Id for commit ID: $cid" >&2
			echo "Please add Change-Id and re-run the script." >&2
			exit 1
		fi
	else
		uniqID="Change-Id=${changeID}"
	fi
	if [ -z "$uniqID" ]; then
		echo "-E- Failed to get unique Id for commit ID: $cid" >&2
		exit 1
	fi
	subject=$(get_subject $cid)
	feature=$(get_by_tag $cid "feature")
	upstream=$(get_by_tag $cid "upstream(.*status)")
	general=$(get_by_tag $cid "general")

	# auto-detect commits that changes only backports, ofed-scripts
	if is_backports_change_only $cid ;then
		feature="backports"
		upstream="ignore"
	fi
	if is_scripts_change_only $cid ;then
		feature="ofed_scripts"
		upstream="ignore"
	fi
	if [ "X$ref_db" != "X" ]; then
		if [ "X$feature" == "X" ]; then
			feature=$(get_feature_from_ref "$uniqID" "$ref_db" "$subject")
		fi
		if [ "X$upstream" == "X" ]; then
			upstream=$(get_upstream_status_from_ref "$uniqID" "$ref_db" "$subject")
		fi
	fi

	if [ "X$feature" == "X" ]; then
		feature=$def_feature
	fi
	if [ "X$upstream" == "X" ]; then
		upstream=$def_ustatus
	fi
	entry="$uniqID; subject=${subject}; feature=${feature}; upstream_status=${upstream}; general=${general};"
	echo "'$entry' to metadata/${author}.csv"
	csvfile="${WDIR}/metadata/${author}.csv"
	if [ $dry_run -eq 0 ]; then
		mkdir -p $WDIR/metadata
		if [ ! -e $csvfile ]; then
			echo "sep=;" > $csvfile
		fi
		if (grep -q -- "$uniqID" $csvfile); then
			echo "-W- $cid '${subject}' already exists in ${author}.csv , skipping..." >&2
			echo >&2
		else
			echo "$entry" >> $csvfile
			if ! (echo $csvfiles | grep -q -- "$csvfile"); then
				csvfiles="$csvfiles $csvfile"
			fi
		fi
	fi
done

if [ $dry_run -eq 0 ]; then
	if [ ! -z "$csvfiles" ]; then
		if [ $no_edit -eq 0 ]; then
			vim -o $csvfiles
		fi
		echo ----------------------------------------------------
		echo "Done, please amend these files to your last commit:"
		echo "$csvfiles"
		echo ----------------------------------------------------
		echo
		if [ $no_verify -eq 0 ]; then
			echo "Going to verify content of metadata files..."
			sleep 3
			for ff in $csvfiles
			do
				cmd="$WDIR/devtools/verify_metadata.sh -p $ff"
				echo "Going to run '$cmd'"
				sleep 2
				$cmd
			done
		fi
	else
		echo "-E- no csv files were updated!"
		exit 3
	fi
fi

