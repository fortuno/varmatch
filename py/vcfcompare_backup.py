# Copyright 2015, Chen Sun
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""
    Author: Chen Sun(chensun@cse.psu.edu)
"""

import sys
versionError = "You are using an old version of python, please upgrade to python 2.7+\n"
if sys.hexversion < 0x02070000:
    print (versionError)
    exit()
#elif sys.hexversion > 0x03000000:
#    print ("python 3")

import subprocess
import argparse
import os
import copy
from lib.red_black_tree import RedBlackTreeMap

citation = 'About algorithm used in VCF-Compare, please refer to "Method for Cross-Validating Variant Call Set" Section in our paper.'+'\n Please cite our paper.'

parser = argparse.ArgumentParser(epilog = citation)
parser.add_argument('-r', '--reference', required=True, help = 'reference vcf file path, usually larger than query vcf file')
parser.add_argument('-q', '--query', required=True, help = 'query vcf file path')
parser.add_argument('-g', '--genome', required=True, help= 'reference genome file path, fasta file format')
parser.add_argument('-p', '--false_positive', help='false positive, i.e. mismatch vcf entries in query vcf file, default=false_positive.vcf', default='false_positive.vcf')
parser.add_argument('-n', '--false_negative', help='false negative, i.e. mismatch vcf entries in reference vcf file, default=false_negative.vcf', default='false_negative.vcf')
#parser.add_argument('-t', '--true_positive', help='true positive bed file position', default='true_positive.bed')
parser.add_argument('-o', '--output', help='output matched variants in stage 2 and 3, default=multi_match.out', default='multi_match.out')
parser.add_argument('-d', '--direct_search', help='if activate, only perform stage 1, default=not activate', action = 'store_true')
parser.add_argument('-c', '--chr', help='chromosome name or id, used for parallel multi genome analysis', default='.')
parser.add_argument('-s', '--stat', help='append statistics result into a file, useful for parallel multi genome analysis', default='stat.txt')
args = parser.parse_args()

#match_set = []

#matched_quality_set = []
#refPos_quality = {}
######################### for debug ###########################
refPos_vcfEntry = {}
quePos_vcfEntry = {}
#ref_match_total = set()
#que_match_total = set()

def direct_search(refPos_snp, quePos_snp):
    global ref_match_total
    global que_match_total
    delList = []
    num = 0
    for key in quePos_snp:
        if key in refPos_snp:
            if refPos_snp[key] == quePos_snp[key]:
                delList.append(key)
                #match_set.append(key)
                num += 1
                #ref_match_total.add(key)
                #que_match_total.add(key)
    match_file = open('direct_search.txt', 'w')
    for key in delList:
        match_string = str(key) + ',' + str(refPos_snp[key]) + '\t' + str(key) + ',' + str(quePos_snp[key]) + '\n'
        match_file.write(match_string)
        #matched_quality_set.append(refPos_quality[key])
        refPos_snp.pop(key, None) # delete value with key
        quePos_snp.pop(key, None)
    match_file.close()

    #with open('matched_quality.txt', 'w') as quality:
    #    for q in matched_quality_set:
    #        quality.write(str(q)+'\n')

    #print ("direct search found:", num)


def modify_sequence(sequence, pos, snpSet):
    if len(snpSet) != 3:
        print ("Error: snp set size not right.")
    ref = snpSet[1]
    alt = snpSet[2]
    if sequence[pos:pos+len(ref)].upper() != ref.upper():
        pass
    result = sequence[:pos] + alt + sequence[pos+len(ref):]
    return result

def near_search(refPos_snp, quePos_snp, genome, blockSize):

    queRemoveList = [] # record quePos that should be deleted
    genomeLen = len(genome) # record genome length
    output = open(args.output, 'a') #open output file for
    if refPos_snp is None:
        print ("Error: refPos_snp is None")
    if quePos_snp is None:
        print ("Error: quePos_snp is None")

    num = 0
    for key in quePos_snp:
        num += 1
        ref_element = refPos_snp.find_nearest(key) # return a position
        ref_snp = ref_element.value()
        que_snp = quePos_snp[key]
        refPos = ref_element.key()
        quePos = key

        if abs(refPos-key) > blockSize:
            continue
        if ref_snp[0] != que_snp[0]:
            continue

        #get the substring
        seqStart = min(key, refPos)-100
        if seqStart < 0:
            seqStart = 0
        seqEnd = max(key, refPos) + 100
        if seqEnd > genomeLen-1:
            seqEnd = genomeLen-1
        subSequence = genome[seqStart:seqEnd+1]
        refIndex = refPos-seqStart
        queIndex = quePos-seqStart

        #modify string and then compare
        refSequence = modify_sequence(subSequence, refIndex, ref_snp)
        queSequence = modify_sequence(subSequence, queIndex, que_snp)
        if refSequence.upper() == queSequence.upper():
            queRemoveList.append(quePos)
            ref_variants = '{},{},{}'.format(refPos, ref_snp[1], ref_snp[2])
            query_variants = '{},{},{}'.format(quePos, que_snp[1], que_snp[2])
            output_info = '{},{}'.format(subSequence, refSequence.upper())
            match_string = '.\t{}\t{}\t.\n'.format(ref_variants, query_variants)
            output.write(match_string)
            refPos_snp.pop(refPos, None)
            break

    output.close()
    for pos in queRemoveList:
        match_set.append(pos)
        quePos_snp.pop(pos, None)

def powerful_near_search(refPos_snp, quePos_snp, genome, blockSize):

    queRemoveList = [] # record quePos that should be deleted
    genomeLen = len(genome) # record genome length
    output = open(args.output, 'a') #open output file for
    if refPos_snp is None:
        print ("Error: refPos_snp is None")
    if quePos_snp is None:
        print ("Error: quePos_snp is None")
    num = 0
    for key in quePos_snp:
        num += 1
        #print num
        minPos = max(key-blockSize, 0)
        maxPos = min(key+blockSize, genomeLen-1)
        que_snp = quePos_snp[key]
        quePos = key
        for (k,v) in refPos_snp.find_range(minPos, maxPos):

            ref_snp = v
            refPos = k
            if ref_snp[0] != que_snp[0]:
                continue

            #get the substring
            seqStart = min(key, refPos)-100
            if seqStart < 0:
                seqStart = 0
            seqEnd = max(key, refPos) + 100
            if seqEnd > genomeLen-1:
                seqEnd = genomeLen-1
            subSequence = genome[seqStart:seqEnd+1]
            refIndex = refPos-seqStart
            queIndex = quePos-seqStart

            #modify string and then compare
            refSequence = modify_sequence(subSequence, refIndex, ref_snp)
            queSequence = modify_sequence(subSequence, queIndex, que_snp)
            if refSequence.upper() == queSequence.upper():
                queRemoveList.append(quePos)
                ref_variants = '{},{},{}'.format(refPos, ref_snp[1], ref_snp[2])
                query_variants = '{},{},{}'.format(quePos, que_snp[1], que_snp[2])
                output_info = '{},{}'.format(subSequence, refSequence.upper())
                match_string = '.\t{}\t{}\t.\n'.format(ref_variants, query_variants)
                output.write(match_string)
                refPos_snp.pop(refPos, None)
                break

    output.close()
    for pos in queRemoveList:
        match_set.append(pos)
        quePos_snp.pop(pos, None)

def modify_by_list(pos_snp, posList, sequence, bound):
    modList = copy.deepcopy(posList)
    modList.sort(reverse=True)
    for pos in modList:
        snp = pos_snp[pos]
        if len(snp) != 3:
            print ("Error: snp set size not right.")
        index = pos-bound
        ref = snp[1]
        alt = snp[2]
        if sequence[index:index+len(ref)].upper() != ref.upper():
            pass
        sequence = sequence[:index] + alt + sequence[index+len(ref):]
    return sequence

def complex_search(refPos_snp, quePos_snp, genome, rev):
    #global ref_match_total
    #global que_match_total
    queRemoveList = [] # record quePos that should be deleted
    genomeLen = len(genome) # record genome length
    output = open(args.output, 'a+') #open output file for
    if refPos_snp is None:
        print ("Error: refPos_snp is None")
    if quePos_snp is None:
        print ("Error: quePos_snp is None")
    num = 0

    start_position = None
    for (key, value) in quePos_snp.find_range(None, None):
        num += 1
        que_snp = value
        quePos = key
        minPos = key
        maxPos = min(key+len(que_snp[1])-1, genomeLen-1) + 1
        candidateRefPos = []
        candidateRefNode = []
        temp_refPos_snp = {}
        min_refPos = 3000000000
        max_refPos = 0
        for p in refPos_snp.linear_range_search(start_position, minPos, maxPos):
            k = p.key()
            v = p.value()
            if min_refPos > k:
                min_refPos = k
            if max_refPos < k:
                max_refPos = k
            candidateRefNode.append(p)
            candidateRefPos.append(k)
            temp_refPos_snp[k] = v
        #get the substring
        if len(candidateRefPos) == 0:
            continue

        before = refPos_snp.before(candidateRefNode[0])
        while before is not None and before.key() + len(before.value()[1]) - 1 >= minPos:
            #print ('find before boundary in stage 2')
            candidateRefNode.insert(0, before)
            min_refPos = before.key()
            candidateRefPos.append(before.key())
            temp_refPos_snp[before.key()] = before.value()
            before = refPos_snp.before(candidateRefNode[0])

        candidateRefPos.sort()
        seqStart = min(key, min_refPos)-100
        if seqStart < 0:
            seqStart = 0
        seqEnd = max(key, max_refPos) + 100
        if seqEnd > genomeLen-1:
            seqEnd = genomeLen-1
        subSequence = genome[seqStart:seqEnd+1]
        queIndex = quePos-seqStart

        #modify string and then compare
        refSequence = modify_by_list(temp_refPos_snp, candidateRefPos, subSequence, seqStart)
        queSequence = modify_sequence(subSequence, queIndex, que_snp)

        if refSequence.upper() == queSequence.upper():
            #matched
            start_position = refPos_snp.after(candidateRefNode[-1])
            queRemoveList.append(quePos)
            ref_variants = ''
            query_variants = ''
            if not rev:
                for index in range(len(candidateRefPos)-1):
                    pos = candidateRefPos[index]
                    ref_variants += '{},{},{};'.format(pos, temp_refPos_snp[pos][1], temp_refPos_snp[pos][2])
                    #ref_match_total.add(pos)
                    #be sure to recover
                    refPos_snp.pop(pos)
                ref_pos = candidateRefPos[-1]
                ref_variants += '{},{},{}'.format(ref_pos, temp_refPos_snp[ref_pos][1], temp_refPos_snp[ref_pos][2])

                #ref_match_total.add(ref_pos)
                # be sure to recover
                refPos_snp.pop(ref_pos)

                #multi_match_ref += 1
                query_variants = '{},{},{}'.format(quePos, que_snp[1], que_snp[2])
            else:
                for index in range(len(candidateRefPos)-1):
                    pos = candidateRefPos[index]
                    query_variants += '{},{},{};'.format(pos, temp_refPos_snp[pos][1], temp_refPos_snp[pos][2])
                    #que_match_total.add(pos)
                    refPos_snp.pop(pos)
                ref_pos = candidateRefPos[-1]
                query_variants += '{},{},{}'.format(ref_pos, temp_refPos_snp[ref_pos][1], temp_refPos_snp[ref_pos][2])
                #que_match_total.add(ref_pos)
                refPos_snp.pop(ref_pos)
                #multi_match_ref += 1
                ref_variants = '{},{},{}'.format(quePos, que_snp[1], que_snp[2])
            output_info = '{},{}'.format(subSequence, refSequence.upper())
            match_string = '.\t{}\t{}\t.\n'.format(ref_variants, query_variants)
            output.write(match_string)
        else:
            start_position = candidateRefNode[0]
    output.close()
    for pos in queRemoveList:
        #match_set.append(pos)
        #que_match_total.add(pos)
        quePos_snp.pop(pos, None)

def multi_search(refPos_snp, quePos_snp, genome, blockSize):
    #global ref_match_total
    #global que_match_total
    multi_match = 0
    multi_match_ref = 0
    multi_match_que = 0
    one2multi = 0
    multi2multi = 0

    genomeLen = len(genome)
    output = open(args.output, 'a+') #open output file for
    refPosDelSet = set()
    quePosDelSet = set()


    #debug = False
    ref_start_position = None
    que_start_position = None
    for key in quePos_snp.keys()[:]:

        if not key in quePos_snp:  # logN operation
            continue

        candidateRefPos = []
        candidateQuePos = []
        candidateRefNode = []
        candidateQueNode = []
        minPos = max(key-blockSize, 0)
        maxPos = min(key+blockSize, genomeLen-1) + 1

        temp_refPos_snp = {}
        for p in refPos_snp.linear_range_search(ref_start_position, minPos, maxPos):
            k = p.key()
            v = p.value()
            candidateRefNode.append(p)
            candidateRefPos.append(k)
            temp_refPos_snp[k] = v

        temp_quePos_snp = {}
        for p in quePos_snp.linear_range_search(que_start_position, minPos, maxPos):
            k = p.key()
            v = p.value()
            candidateQueNode.append(p)
            candidateQuePos.append(k)
            temp_quePos_snp[k] = v

        if len(candidateQuePos) == 0:
            print ("Error: query empty")
            continue

        if len(candidateRefPos) == 0:
            continue
        min_ref_pos = candidateRefPos[0]
        max_ref_pos = candidateRefPos[-1] + len(temp_refPos_snp[candidateRefPos[-1]][1]) - 1

        min_que_pos = candidateQuePos[0]
        max_que_pos = candidateQuePos[-1] + len(temp_quePos_snp[candidateQuePos[-1]][1]) - 1

        """
        ref_before = refPos_snp.before(candidateRefNode[0])
        que_before = quePos_snp.before(candidateQueNode[0])
        while (ref_before is not None and ref_before.key() + len(ref_before.value()[0]) - 1 >= min_que_pos) or (que_before is not None and que_before.key() + len(que_before.value()[0])-1 > min_ref_pos):
            #print ('find before boundary in stage 3')
            if ref_before is not None and ref_before.key() + len(ref_before.value()[0]) - 1 >= min_que_pos :
                candidateRefNode.insert(0, ref_before)
                min_ref_pos = ref_before.key()
                candidateRefPos.insert(0, ref_before.key())
                temp_refPos_snp[ref_before.key()] = ref_before.value()
                ref_before = refPos_snp.before(candidateRefNode[0])

            if que_before is not None and que_before.key() + len(que_before.value()[0]) - 1 >= min_ref_pos :
                candidateQueNode.insert(0, que_before)
                min_que_pos = que_before.key()
                candidateQuePos.insert(0, que_before.key())
                temp_quePos_snp[que_before.key()] = que_before.value()
                que_before = quePos_snp.before(candidateQueNode[0])

        ref_after = refPos_snp.after(candidateRefNode[-1])
        que_after = quePos_snp.after(candidateQueNode[-1])
        while (ref_after is not None and ref_after.key() <= max_que_pos) or (que_after is not None and que_after.key() <= max_ref_pos):
            #print ('find after boundary in stage 3')
            if ref_after is not None and ref_after.key() <= max_que_pos :
                candidateRefNode.append(ref_after)
                max_ref_pos = ref_after.key() + len(ref_after.value()[1]) - 1
                candidateRefPos.append(ref_after.key())
                temp_refPos_snp[ref_after.key()] = ref_after.value()
                ref_after = refPos_snp.after(candidateRefNode[-1])

            if que_after is not None and que_after.key() <= max_ref_pos :
                candidateQueNode.append(que_after)
                max_que_pos = que_after.key() + len(que_after.value()[1]) - 1
                candidateQuePos.append(que_after.key())
                temp_quePos_snp[que_after.key()] = que_after.value()
                que_after = quePos_snp.after(candidateQueNode[-1])

        """
        lowBound = candidateRefPos[0]
        upperBound = candidateRefPos[-1]


        if lowBound > candidateQuePos[0]:
            lowBound = candidateQuePos[0]
        if upperBound < candidateQuePos[-1]:
            upperBound = candidateQuePos[-1]

        lowBound = max(0, lowBound-100)
        upperBound = min(upperBound+100, genomeLen-1)

        subSequence = genome[lowBound: upperBound+1]

        refSequence = modify_by_list(temp_refPos_snp, candidateRefPos, subSequence, lowBound)
        queSequence = modify_by_list(temp_quePos_snp, candidateQuePos, subSequence, lowBound)

        if refSequence.upper() == queSequence.upper():
            #print ("multi_search works")
            ref_start_position = refPos_snp.after(candidateRefNode[-1])
            que_start_position = quePos_snp.after(candidateQueNode[-1])
            ref_variants = ''
            query_variants = ''
            for index in range(len(candidateRefPos)-1):
                pos = candidateRefPos[index]
                ref_variants += '{},{},{};'.format(pos, temp_refPos_snp[pos][1], temp_refPos_snp[pos][2])
                #ref_match_total.add(pos)
                refPos_snp.pop(pos)
                multi_match_ref += 1
            ref_pos = candidateRefPos[-1]
            ref_variants += '{},{},{}'.format(ref_pos, temp_refPos_snp[ref_pos][1], temp_refPos_snp[ref_pos][2])
            #ref_match_total.add(ref_pos)
            refPos_snp.pop(ref_pos)
            multi_match_ref += 1

            for index in range(len(candidateQuePos)-1):
                pos = candidateQuePos[index]
                query_variants += '{},{},{};'.format(pos, temp_quePos_snp[pos][1], temp_quePos_snp[pos][2])
                #que_match_total.add(pos)
                quePos_snp.pop(pos)
                #quePosList.remove(pos)
                #match_set.append(pos)
                multi_match_que += 1
                #quePosDelSet.add(pos)
            que_pos = candidateQuePos[-1]
            query_variants += '{},{},{}'.format(que_pos, temp_quePos_snp[que_pos][1], temp_quePos_snp[que_pos][2])
            #que_match_total.add(que_pos)
            quePos_snp.pop(que_pos)
            #quePosList.remove(que_pos)
            #match_set.append(que_pos)
            multi_match_que += 1
            #quePosDelSet.add(que_pos)

            output_info = '{},{},{},{},{}'.format(blockSize, lowBound, upperBound+1, subSequence, refSequence.upper())
            match_string = '.\t{}\t{}\t{}\n'.format(ref_variants, query_variants, output_info)
            output.write(match_string)
            multi_match += 1

            if len(candidateRefPos) == 1 or len(candidateQuePos) == 1:
                one2multi += 1
            else:
                multi2multi += 1
        else:
            ref_start_position = candidateRefNode[0]
            que_start_position = candidateQueNode[0]

    output.close()
    #print (multi_match, multi_match_ref, multi_match_que, one2multi, multi2multi)


def report(refPos_snp, quePos_snp, refOriginalNum, queOriginalNum):
    positiveFile = open(args.false_positive, "a+")
    negativeFile = open(args.false_negative, "a+")

    #query_mismatch_file = open(args.false_positive, 'w')
    #ref_mismatch_file = open(args.false_negative, 'w')

    #true_pos_file = open(args.true_positive, 'w')

    refList = list(refPos_snp.keys())
    refList.sort()
    for pos in refList:
        #s = args.chr + "\t" + str(pos) + "\t" + str(pos+1) + "\n"
        s = refPos_vcfEntry[pos] + '\n'
        negativeFile.write(s)
    negativeFile.close()

    queList = list(quePos_snp.keys())
    queList.sort()
    for pos in queList:
        #s = args.chr + "\t" + str(pos) + "\t" + str(pos+1) + "\n"
        s = quePos_vcfEntry[pos] + '\n'
        positiveFile.write(s)
    positiveFile.close()

    #match_set.sort()
    #for pos in match_set:
    #    s = args.chr + '\t' + str(pos) + '\t' + str(pos+1) + '\n'
    #    true_pos_file.write(s)
    #true_pos_file.close()

    print ('\n######### Matching Result ################\n')
    print (' ref total: {}\n que total: {}\n ref matches: {}\n que matches: {}\n ref mismatch: {}\n alt mismatch: {}\n'.format(refOriginalNum, queOriginalNum,refOriginalNum-len(refPos_snp), queOriginalNum-len(quePos_snp) , len(refPos_snp), len(quePos_snp)))

    stat_file = open(args.stat, 'a+')
    stat_file.write('{}\t{}\t{}\t{}\t{}\n'.format(args.chr, refOriginalNum, queOriginalNum, refOriginalNum-len(refPos_snp), queOriginalNum-len(quePos_snp)))
    stat_file.close()
    #print (len(ref_match_total), len(que_match_total))
    #print multi_match, multi_match_ref, multi_match_que


def main():
    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit()

##################################################################################
    if not os.path.isfile(args.reference):
        print ("Error: reference file not found")
        parser.print_help()
        sys.exit()
    if not os.path.isfile(args.query):
        print ("Error: query vcf file not found.")
        parser.print_help()
        sys.exit()
    if not os.path.isfile(args.genome):
        print("Error: genome file not found.")
        parser.print_help()
        sys.exit()

    report_head = '##genome=' + args.genome + '\n'
    report_head += '##ref=' + args.reference + '\n'
    report_head += '##query=' + args.query + '\n'
    report_head += '##chr_name=chromosome name of this data\n'
    report_head += '##ref_variant=matched variant from reference set\n'
    report_head += '##query_variant=matched variants from query set, corresponding to ref_variant\n'
    report_head += '##variants in both ref_variants and query_variants are separated by ";"\n'
    report_head += '##each variant is a tuple<POS,REF,ALT> separated by ",", POS is 0-based position, REF is sequence in reference genome, ALT is corresponding allele in donor genome\n'
    report_head += '##info=matching information, if directly matched, there will be "."; if >1 variants in ref_variants or query_variants, info will be subsequence from genome, and the modified subsequence by ref_variants and query_variants\n'
    report_head += '#chr_name\tref_variants\tquery_variants\tinfo\n'

    with open(args.output, 'w') as output:
        output.write(report_head)

    sequence = ""

    print ('read genome file...')
    seqFile = open(args.genome)
    for line in seqFile.readlines():
        if line.startswith(">"):
            continue
        line = line.strip()
        sequence += line
    seqFile.close()


    ref_mismatch_file = open(args.false_negative, 'w')

    print ('read reference vcf file...')
    hash_refPos_snp = {}
    #refPos_snp = RedBlackTreeMap()
    refFile = open(args.reference)
    for line in refFile.readlines():
        if line.startswith("#"):
            ref_mismatch_file.write(line)
            continue
        line = line.strip()
        columns = line.split("\t")
        pos = int(columns[1])-1
        ref = columns[3]
        alt = columns[4]
        quality = columns[6]
        if ',' in alt:
            continue
        snpType = 'S'
        if len(ref) > len(alt):
            snpType = 'D'
        elif len(ref) < len(alt):
            snpType = 'I'
        #print pos, snpType, ref, alt
        hash_refPos_snp[pos] = [snpType, ref, alt]
        refPos_vcfEntry[pos] = line
        #refPos_quality[pos] = quality
    refFile.close()

    ref_mismatch_file.close()

    que_mismatch_file = open(args.false_positive, 'w')

    print ('read query vcf file...')
    hash_quePos_snp = {}
    #quePos_snp = RedBlackTreeMap()
    queFile = open(args.query)
    for line in queFile.readlines():
        if line.startswith("#"):
            que_mismatch_file.write(line)
            continue
        line = line.strip()
        columns = line.split("\t")
        pos = int(columns[1])-1
        ref = columns[3]
        alt = columns[4]
        if ',' in alt:
            continue
        snpType = 'S'
        if len(ref) > len(alt):
            snpType = 'D'
        elif len(ref) < len(alt):
            snpType = 'I'
        hash_quePos_snp[pos] = [snpType, ref, alt]
        quePos_vcfEntry[pos] = line
    queFile.close()

    que_mismatch_file.close()

    refOriginalNum = len(hash_refPos_snp)
    queOriginalNum = len(hash_quePos_snp)

    print ('first stage start...')
    if refOriginalNum > 0 and queOriginalNum > 0:
        direct_search(hash_refPos_snp, hash_quePos_snp)

    #print ("after direct search: ", len(hash_refPos_snp), len(hash_quePos_snp))

    if args.direct_search:
        report(hash_refPos_snp, hash_quePos_snp, refOriginalNum, queOriginalNum)
        return

    refPos_snp = RedBlackTreeMap()
    quePos_snp = RedBlackTreeMap()

    for k in hash_refPos_snp:
        refPos_snp[k] = hash_refPos_snp[k]

    for k in hash_quePos_snp:
        quePos_snp[k] = hash_quePos_snp[k]

    print ('second stage start...')

    if len(refPos_snp) > 0 and len(quePos_snp) > 0:
        complex_search(refPos_snp, quePos_snp, sequence, False)

    if len(refPos_snp) > 0 and len(quePos_snp) > 0:
        complex_search(quePos_snp, refPos_snp, sequence, True)
    #print ("after complex search:", len(refPos_snp), len(quePos_snp))

    print ('third stage start...')
    for block_size in [2, 4, 5,10,20,50,100,200]:
        print ('try window size ' + str(block_size*2) + '...')
        if len(refPos_snp) > 0 and len(quePos_snp) > 0:
            multi_search(refPos_snp, quePos_snp, sequence, block_size)
        #print ('after multi search in ' + str(block_size) + ' bp range:', len(refPos_snp), len(quePos_snp))

    report(refPos_snp, quePos_snp, refOriginalNum, queOriginalNum)

if __name__ == '__main__':
    main()
