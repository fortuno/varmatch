#include "vcf.h"


bool operator <(const SNP& x, const SNP& y) {
	return x.pos < y.pos;
}

bool operator ==(const SNP& x, const SNP& y) {
	if (x.pos == y.pos && x.snp_type == y.snp_type && x.alt == y.alt) {
		return true;
	}
	return false;
}

VCF::VCF(int thread_num_)
{
    debug_f = 0;
	genome_sequence = "";
	boundries_decided = false;
    complex_search = false;
	clustering_search = false;
    if (thread_num_ == 0) {
		thread_num = 1;
	}
	else {
		thread_num = min(thread_num_, (int)thread::hardware_concurrency());
	}
	dout << "Thread Number: " << thread_num << endl;
    chromosome_name = ".";
}


VCF::~VCF()
{
}



void VCF::ReadVCF(string filename, SnpHash & pos_2_snp) {
	if (!boundries_decided) {
		cout << "[Error] VCF::ReadVCF cannot read vcf file before read genome file" << endl;
		return;
	}

	ifstream vcf_file;
	vcf_file.open(filename.c_str());
	if (!vcf_file.good()) {
		cout << "[Error] VCF::ReadVCF can not open vcf file" << endl;
		return;
	}
    string previous_line;
	while (!vcf_file.eof()) { // alternative way is vcf_file != NULL
		string line;
		getline(vcf_file, line, '\n');
		//dout << line << endl;
		if ((int)line.length() <= 1) continue;
		if (line[0] == '#') continue;
		auto columns = split(line, '\t');
		if(chromosome_name == ".") chromosome_name = columns[0];
        auto pos = atoi(columns[1].c_str()) - 1;
		auto ref = columns[3];
		auto alt = columns[4];
		auto quality = columns[6];

		if (alt.find(",") != string::npos) continue; // can not deal with multi alt yet
        //todo(Chen) deal with multi alt

		char snp_type = 'S'; 
		if ((int)ref.length() > (int)alt.length()) {
			snp_type = 'D';
		}
		else if ((int)ref.length() < (int)alt.length()) {
			snp_type = 'I';
		}

		//decide which thread to use
		int index = 0;
		for (int i = 0; i < pos_boundries.size(); i++) {
			if (pos < pos_boundries[i]) {
				index = i;
				break;
			}
		}

		if (pos_2_snp[index].find(pos) != pos_2_snp[index].end()) continue; // can not deal with multi alt yet
		pos_2_snp[index][pos].push_back(SNP(pos, snp_type, ref, alt));
        //if(pos_2_snp[index][pos].size() > 1){
        //    dout << previous_line << endl;
        //    dout << line << endl;
        //}

	}
	vcf_file.close();
	return;
}

void VCF::ReadGenomeSequence(string filename) {
	ifstream genome_file;
	genome_file.open(filename.c_str());
	if (!genome_file.good()) {
		cout << "[Error] VCF::ReadGenomeSequence can not open fasta file" << endl;
		return;
	}

	genome_sequence = "";

	while(!genome_file.eof()) {
		string line;
		getline(genome_file, line, '\n');
		if ((int)line.length() <= 1) continue;
		if (line[0] == '>') continue;
		genome_sequence += line;
	}
	genome_file.close();
	// boundries can get after knowing genome sequence.
	DecideBoundries();
	return;
}

void VCF::DecideBoundries() {
	int genome_size = genome_sequence.size();

	int distance = genome_size / thread_num;
	for (int i = 0; i < thread_num - 1; i++) {
		pos_boundries.push_back((i + 1)*distance);
	}
	pos_boundries.push_back(genome_size);

	// initialize two for copy
	unordered_map<int, vector<SNP> > ref_h;
	unordered_map<int, vector<SNP> > que_h;
	map<int, vector<SNP> > ref_m;
	map<int, vector<SNP> > que_m;

	for (int i = 0; i < thread_num; i++) {
		refpos_2_snp.push_back(ref_h);
		querypos_2_snp.push_back(que_h);
		refpos_snp_map.push_back(ref_m);
		querypos_snp_map.push_back(que_m);
	}

	boundries_decided = true;

}

void VCF::ReadRefVCF(string filename) {
	ReadVCF(filename, refpos_2_snp);
}

void VCF::ReadQueryVCF(string filename) {
	ReadVCF(filename, querypos_2_snp);
}

bool VCF::CompareSnps(SNP r, SNP q) {
	auto ref_ref = r.ref;
	transform(ref_ref.begin(), ref_ref.end(), ref_ref.begin(), ::toupper);
	auto ref_alt = r.alt;
	transform(ref_alt.begin(), ref_alt.end(), ref_alt.begin(), ::toupper);
	auto que_ref = q.ref;
	transform(que_ref.begin(), que_ref.end(), que_ref.begin(), ::toupper);
	auto que_alt = q.alt;
	transform(que_alt.begin(), que_alt.end(), que_alt.begin(), ::toupper);
	if (ref_ref == que_ref && ref_alt == que_alt) return true;
	return false;
}

void VCF::DirectSearchInThread(unordered_map<int, vector<SNP> > & ref_snps, unordered_map<int, vector<SNP> > & query_snps) {
	auto rit = ref_snps.begin();
	auto rend = ref_snps.end();
	for (; rit != rend;) {
		auto r_pos = rit->first;
		auto & r_snps = rit->second;
		auto qit = query_snps.find(r_pos);
		if (qit != query_snps.end()) {
			
			auto & q_snps = qit->second;

			if (r_snps.size() != 1 || q_snps.size() != 1) {
				//for(int i = 0; i < r_snps.size(); i++)
                //    dout << "r_snp: " << r_snps[i].alt << endl;
                //for(int i = 0; i < q_snps.size(); i++)
                //    dout << "q_snp: " << q_snps[i].alt << endl;
                //cout << "[Error] snp vector size not right" << endl;
			}

			vector<vector<SNP>::iterator> r_deleted_snps;
			vector<vector<SNP>::iterator> q_deleted_snps;
			for (auto r_snp_it = r_snps.begin(); r_snp_it != r_snps.end(); ++r_snp_it) {
				for (auto q_snp_it = q_snps.begin(); q_snp_it != q_snps.end(); ++q_snp_it) {
					if (CompareSnps(*r_snp_it, *q_snp_it)) {
						r_deleted_snps.push_back(r_snp_it);
						q_deleted_snps.push_back(q_snp_it);
					}
				}
			}
			for (int i = 0; i < r_deleted_snps.size(); i++) {
				r_snps.erase(r_deleted_snps[i]);
			}
			if (r_snps.size() == 0) {
				rit = ref_snps.erase(rit);
			}
			else {
				++rit;
			}
			for (int i = 0; i < q_deleted_snps.size(); i++) {
				q_snps.erase(q_deleted_snps[i]);
			}
			if (q_snps.size() == 0) {
				query_snps.erase(qit);
			}
		}else{
            ++rit;
        }
	}
}

// directly match by position
void VCF::DirectSearchMultiThread() {
	vector<thread> threads;
	//spawn threads
	unsigned i = 0;
	for (; i < thread_num - 1; i++) {
		threads.push_back( thread(&VCF::DirectSearchInThread, this, ref(refpos_2_snp[i]), ref(querypos_2_snp[i])) );
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
	DirectSearchInThread(refpos_2_snp[i], querypos_2_snp[i]);

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));

    threads.clear();
}

string VCF::ModifySequenceBySnp(const string sequence, SNP s, int offset) {
	// [todo] unit test
	string result = "";
	int snp_pos = s.pos - offset;
	int snp_end = snp_pos + (int)s.ref.length();
	if(snp_end > (int)sequence.length()){
        dout << "[Error] snp end greater than sequence length" << endl;
    }
    //if(snp_end > sequence.length()){
    //    cout << "snp end greater than sequence length" << endl;
    //    cout << snp_end << "\t" << sequence.length() << endl;
    //}
	result += sequence.substr(0, snp_pos);
	result += s.alt;
	result += sequence.substr(snp_end, sequence.length() - snp_end);
	transform(result.begin(), result.end(), result.begin(), ::toupper);
	return result;
}
string VCF::ModifySequenceBySnpList(const string sequence, vector<SNP> s, int offset) {
	// [todo] unit test
	string result = sequence;
	int start_pos = 0;
    if(s.size() == 1){
        return ModifySequenceBySnp(sequence, s[0], offset);
    }
    sort(s.begin(), s.end());
    //dout << sequence << endl;
	for (int i = s.size()-1; i >= 0; i--) {
		int snp_pos = s[i].pos - offset;
		int snp_end = snp_pos + (int)s[i].ref.length();
		string snp_alt = s[i].alt;
		//result += sequence.substr(start_pos, snp_pos - start_pos);
		//result += snp_alt;
		//start_pos = snp_end;
        int result_length = (int)result.length();
        if(snp_pos > result_length || snp_end > result_length){
        //    dout << "[Warning] overlapping variants detected" << endl;
        //    dout << "=============================" << endl;
	    //    dout << result << endl;
        //    dout << snp_pos << "," << snp_end << "," << s[i].ref << "," << s[i].alt << endl;
            result = sequence;
            transform(result.begin(), result.end(), result.begin(), ::toupper);
            return result;
        }
        result = result.substr(0, snp_pos) + s[i].alt + result.substr(snp_end, result_length-snp_end);
    }
	//if (start_pos < sequence.length()) {
	//	result += sequence.substr(start_pos, sequence.length() - start_pos);
	//}
	transform(result.begin(), result.end(), result.begin(), ::toupper);
	return result;
}

bool VCF::CheckVariantOverlap(vector<SNP> snp_list){
    if (snp_list.size() <= 1) return false;
    int previous_ends = -1;
    for(int i = 0; i < snp_list.size(); i++){
        if(snp_list[i].pos < previous_ends) return true;
        if( previous_ends < snp_list[i].pos + (int)snp_list[i].ref.length()){
            previous_ends = snp_list[i].pos + (int)snp_list[i].ref.length();
        }
    }
    return false;
}

bool VCF::ComplexMatch(SNP s, vector<SNP> comb) {
	//size of comb >= 1
    assert(comb.size() >= 1);
	sort(comb.begin(), comb.end());
	int ref_left = s.pos;
	int ref_right = ref_left + (int)s.ref.length();

	int comb_size = comb.size();
	int que_left = comb[0].pos;
	int que_right = comb[comb_size - 1].pos + (int)(comb[comb_size - 1].ref.length());

	int genome_left = min(ref_left, que_left);
	int genome_right = max(ref_right, que_right);

	genome_left = max(0, genome_left - 10);
	genome_right = min(genome_right + 10, (int)genome_sequence.length());

	string subsequence = genome_sequence.substr(genome_left, genome_right - genome_left);
	return ModifySequenceBySnp(subsequence, s, genome_left) == ModifySequenceBySnpList(subsequence, comb, genome_left);
}

bool VCF::ExponentialComplexMatch(SNP r_snp,
								map<int, vector<SNP> > & query_snps,
								map<int, vector<SNP> >::iterator & qit_start,
								vector<SNP> & deleted_ref_snps,
								vector<SNP> & deleted_que_snps)
{

	int ref_start_pos = r_snp.pos;
	auto ref_ref = r_snp.ref;
	auto ref_alt = r_snp.alt;
    int ref_change = (int)(ref_alt.length()) - (int)(ref_ref.length());
	int ref_end_pos = ref_start_pos + ref_ref.size();
	vector<SNP> candidate_query_list;
    vector<int> candidate_changes;

	FindVariantsInRange_NlgN(ref_start_pos,
							ref_end_pos,
							query_snps,
							candidate_query_list,
							candidate_changes);

	//[todo] unit test function FindVariantsInRange_Linear, time and accuracy

	//FindVariantsInRange_Linear(ref_start_pos,
	//						ref_end_pos,
	//						query_snps,
	//						qit_start,
	//						candidate_query_list,
	//						candidate_changes);


	int candidate_size = candidate_query_list.size();
	if (candidate_size == 0) return false;
	//check all combinations, from largest, one single match is enough
	bool flag = false;
	for (int k = candidate_query_list.size(); k >= 1; --k) {
		vector<vector<SNP>> combinations = 
            CreateCombinationsWithTarget(candidate_query_list,
                    k,
                    candidate_changes,
                    ref_change);
		//vector<vector<SNP>> combinations = CreateCombinations(candidate_query_list, k);
		bool matched = false;
		// check combinations with k elements
		for (auto cit = combinations.begin(); cit != combinations.end(); ++cit) {
			auto comb = *cit;
			if (ComplexMatch(r_snp, *cit)) {
				matched = true;

				// delete corresponding snps
				deleted_ref_snps.push_back(r_snp);
				for (auto combit = comb.begin(); combit != comb.end(); ++combit) {
					deleted_que_snps.push_back(*combit);
				}
				break;
			}
		}
		if (matched) {
			flag = true;
			break;
		}
	}
	return flag;
}

unsigned int VCF::EditDistance(const std::string& s1, const std::string& s2)
{
	const std::size_t len1 = s1.size(), len2 = s2.size();
	std::vector<unsigned int> col(len2 + 1), prevCol(len2 + 1);

	for (unsigned int i = 0; i < prevCol.size(); i++)
		prevCol[i] = i;
	for (unsigned int i = 0; i < len1; i++) {
		col[0] = i + 1;
		for (unsigned int j = 0; j < len2; j++)
			// note that std::min({arg1, arg2, arg3}) works only in C++11,
			// for C++98 use std::min(std::min(arg1, arg2), arg3)
			col[j + 1] = std::min({ prevCol[1 + j] + 1, col[j] + 1, prevCol[j] + (s1[i] == s2[j] ? 0 : 1) });
		col.swap(prevCol);
	}
	return prevCol[len2];
}

bool VCF::GreedyComplexMatch(SNP r_snp,
							map<int, vector<SNP> > & query_snps,
							vector<SNP> & deleted_ref_snps,
							vector<SNP> & deleted_que_snps)
{
    //dout << "==" << endl;
	int ref_start_pos = r_snp.pos;
	auto ref_ref = r_snp.ref;
	auto ref_alt = r_snp.alt;
	int ref_end_pos = ref_start_pos + ref_ref.size();

	auto itlow = query_snps.lower_bound(ref_start_pos);
	auto itup = query_snps.upper_bound(ref_end_pos);
	// theoretically, since all snps are independent, we do not need the following two if-statement
	// but this is engineering
	if (itlow != query_snps.begin()) {
		itlow--;
	}
	if (itup != query_snps.end()) {
		itup++;
	}

	// we have candidate query snps stored separatedly in two data structures
	// one according to positions
	map<int, vector<SNP> > candidate_query_map;
	// one purely stored
	vector<SNP> comb;

    int que_left = std::numeric_limits<int>::max();
    int que_right = 0;
	for (auto it = itlow; it != itup; ++it) {
		auto v = it->second;
        bool flag = false;
		for (int i = 0; i < v.size(); i++) {
			int snp_start = v[i].pos;
			int snp_end = snp_start + (int)(v[i].ref.length());
			//if(ref_start_pos <= snp_start && ref_end_pos >= snp_start){
            if (min(ref_end_pos, snp_end) - max(ref_start_pos, snp_start) > 0) {
				comb.push_back(v[i]);
                if (que_left > snp_start ) que_left = snp_start;
                if (que_right < snp_end) que_right = snp_end;
                flag = true;
                //dout << "candidates:" << ref_start_pos << "," << ref_end_pos << ": ";
                //dout << snp_start << "," << snp_end << endl;
                //dout << v[i].pos << "\t" << v[i].ref << "\t" << v[i].alt << endl;
			}
		}
        if(flag){
		    candidate_query_map[it->first] = v;
        }
	}

	if (comb.size() < 1) return false;
	sort(comb.begin(), comb.end());

	int ref_left = r_snp.pos;
	int ref_right = ref_left + (int)(r_snp.ref.length());

	int comb_size = comb.size();
	//int que_left = comb[0].pos;
	//int que_right = comb[comb_size - 1].pos + comb[comb_size - 1].ref.length();

	int genome_left = min(ref_left, que_left);
	int genome_right = max(ref_right, que_right);

	genome_left = max(0, genome_left - 1);
	genome_right = min(genome_right + 1, (int)genome_sequence.length());

	string subsequence = genome_sequence.substr(genome_left, genome_right - genome_left);
	string ref_subseq = ModifySequenceBySnp(subsequence, r_snp, genome_left);
	string que_subseq = subsequence;
	int edit_distance = EditDistance(ref_subseq, que_subseq);
    int len_distance = std::abs((int)(ref_subseq.length() - que_subseq.length()));
	int que_offset = genome_left;

	vector<SNP> deleted_snps;
    //dout << "genome sequence:" << que_subseq << endl;
    //dout << r_snp.pos << "\t" << r_snp.ref << "\t" << r_snp.alt << endl;
    //dout << "ref:" << ref_subseq << endl;
	for (auto it = candidate_query_map.begin(); it != candidate_query_map.end(); ++it) {
		auto v = it->second;
		SNP min_s;
		int min_distance = std::numeric_limits<int>::max();
        int min_len_distance = min_distance;
		string min_subseq;
		for (int i = 0; i < v.size(); i++) {
			auto s = v[i];
            //dout << que_subseq << "\t" << s.pos << "\t" << que_offset << endl;
            //dout << s.pos << "\t" << s.ref << "\t" << s.alt << endl;
			auto subseq = ModifySequenceBySnp(que_subseq, s, que_offset);
			int distance = EditDistance(ref_subseq, subseq);
            int len_dis = abs((int)(ref_subseq.size()- subseq.size()));
			if (distance < min_distance && len_dis <= min_len_distance) {
				min_distance = distance;
				min_s = s;
				min_subseq = subseq;
                min_len_distance = len_dis;
			}
		}
		if (min_distance < edit_distance && min_len_distance <= len_distance) {
			//dout << "distance: " << edit_distance << "," << min_distance << endl;
            edit_distance = min_distance;
			que_subseq = min_subseq;
			que_offset += ((int)(min_s.ref.length()) - (int)(min_s.alt.length()));
			deleted_snps.push_back(min_s);
		}
	}

	if (edit_distance == 0) {
		deleted_ref_snps.push_back(r_snp);
		for (int i = 0; i < deleted_snps.size(); i++)
			deleted_que_snps.push_back(deleted_snps[i]);

		return true;
	}
	else {
		return false;
	}

	return true;
}

void VCF::FindVariantsInRange_NlgN(int start,
								int end,
								map<int, vector<SNP> > snp_map,
								vector<SNP> & candidate_query_list,
								vector<int>& candidate_changes)
{
	auto itlow = snp_map.lower_bound(start);
	auto itup = snp_map.upper_bound(end);
	// theoretically, since all snps are independent, we do not need the following two if-statement
	// but this is engineering
	if (itlow != snp_map.begin()) {
		itlow--;
	}
	if (itup != snp_map.end()) {
		itup++;
	}

	for (auto it = itlow; it != itup; ++it) {
		auto v = it->second;
		for (int i = 0; i < v.size(); i++) {
			int snp_start = v[i].pos;
			int snp_end = snp_start + (int)(v[i].ref.length());
            int change = (int)(v[i].alt.length()) - (int)(v[i].ref.length());
			//if(end > snp_start && start >= snp_start){
            if (min(end, snp_end) - max(start, snp_start) > 0) {
				candidate_query_list.push_back(v[i]);
                candidate_changes.push_back(change);
			}
		}
	}
}

void VCF::FindVariantsInRange_Linear(int start,
	int end,
	map<int, vector<SNP> > snp_map,
	map<int, vector<SNP> >::iterator & qit_start,
	vector<SNP> & candidate_query_list,
	vector<int> & candidate_changes)
{
	//[todo] unit test
	for (auto qit = qit_start; qit != snp_map.end(); ++qit) {
		int que_start_pos = qit->first;
		auto que_snp_list = qit->second;
		//[todo] double check the boundry condition
		if (que_start_pos >= start && que_start_pos < end) {
			for (int j = 0; j < que_snp_list.size(); j++) {
				candidate_query_list.push_back(que_snp_list[j]);
				int change = (int)(que_snp_list[j].alt.length()) - (int)(que_snp_list[j].ref.length());
				candidate_changes.push_back(change);
			}
			//qit_start = qit;
		}
		else if (que_start_pos < start) {
			qit_start = qit;
		}
		else if (que_start_pos >= end) {
			break;
		}
	}
}

void VCF::ComplexSearchInThread(map<int, vector<SNP> > & ref_snps, map<int, vector<SNP> > & query_snps) {
	// linear algorithm
	vector<SNP> deleted_ref_snps;
	vector<SNP> deleted_que_snps;
	// for each position in ref, i.e. a vector
	//dout << ref_snps.size() << endl;
    //dout << query_snps.size() << endl;
    //int i = 0;
	auto qit_start = query_snps.begin();
	// iterator all snps in ref_snps using two for-loops
    for (auto rit = ref_snps.begin(); rit != ref_snps.end(); ++rit) {
        //i ++;
        //dout << i << endl;
		int ref_start_pos = rit->first;
		auto ref_snp_list = rit->second;
		// for each snp in the vector
		for (int i = 0; i < ref_snp_list.size(); i++) {
			auto ref_snp = ref_snp_list[i];

			//ExponentialComplexMatch(ref_snp_list[i], query_snps, qit_start, deleted_ref_snps, deleted_que_snps);

			GreedyComplexMatch(ref_snp_list[i], query_snps, deleted_ref_snps, deleted_que_snps);
		}
	}

	// delete all snps, first check position, then delete by value matching
	// [todo] this actually should be one separated function

	for (auto it = deleted_ref_snps.begin(); it != deleted_ref_snps.end(); ++it) {
		auto snp = *it;
		auto pos = snp.pos;
		auto & v = ref_snps[pos];
		auto vit = v.begin();
		while (vit != v.end()) {
			if (snp == *vit) {
				vit = v.erase(vit);
				break;
			}
			else {
				++vit;
			}
		}
		if (v.size() == 0) {
			ref_snps.erase(ref_snps.find(pos));
		}
	}

	for (auto it = deleted_que_snps.begin(); it != deleted_que_snps.end(); ++it) {
		auto snp = *it;
		auto pos = snp.pos;
		auto & v = query_snps[pos];
		auto vit = v.begin();
		while (vit != v.end()) {
			if (snp == *vit) {
				vit = v.erase(vit);
                break;
			}
			else {
				++vit;
			}
		}
		if (v.size() == 0) {
			query_snps.erase(query_snps.find(pos));
		}
	}
}

void f(){
    this_thread::sleep_for(chrono::seconds(2));
    cout << "Hello World" << endl;
}
// match by overlapping reference region
void VCF::ComplexSearchMultiThread() {
	if (GetRefSnpNumber() == 0 || GetQuerySnpNumber() == 0) return;
    complex_search = true;

	// transfer data from hash to map
	for (int i = 0; i < refpos_2_snp.size(); i++) {
		auto & pos_snp_hash = refpos_2_snp[i];
		for (auto rit = pos_snp_hash.begin(); rit != pos_snp_hash.end(); ++rit) {
			//refpos_snp_map[i][rit->first] = rit->second;
            auto p = rit->first;
            auto v = rit->second;
            for(int j  = 0; j < v.size(); j++){
                refpos_snp_map[i][p].push_back(v[j]);
            }
		}
	}
	refpos_2_snp.clear();

	for (int i = 0; i < querypos_2_snp.size(); i++) {
		auto & pos_snp_hash = querypos_2_snp[i];
		for (auto qit = pos_snp_hash.begin(); qit != pos_snp_hash.end(); ++qit) {
			//querypos_snp_map[i][qit->first] = qit->second;
            auto p = qit->first;
            auto v = qit->second;
            for(int j = 0; j < v.size(); j++){
                querypos_snp_map[i][p].push_back(v[j]);
            }
		}
	}
	querypos_2_snp.clear();

	vector<thread> threads;
	//spawn threads
	unsigned i = 0;
	for (; i < thread_num - 1; i++) {
        if(i >= refpos_snp_map.size() || i >= querypos_snp_map.size()){
            dout << "[Error] index out of map range" << endl;
            continue;
        }
        if(refpos_snp_map[i].size() == 0 || querypos_snp_map[i].size() == 0){
            continue;
        }
		//threads.push_back(thread(f));
        //dout << "create new thread" << endl;
        threads.push_back(thread(&VCF::ComplexSearchInThread, this, ref(refpos_snp_map[i]), ref(querypos_snp_map[i])));
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
    if(i >= refpos_snp_map.size() || i >= querypos_snp_map.size()){
        dout << "[Error] index out of map range" << endl;
    }else if(refpos_snp_map[i].size() != 0 && querypos_snp_map[i].size() != 0){
	    ComplexSearchInThread(refpos_snp_map[i], querypos_snp_map[i]);
    }

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));

    //[todo] check boundary
    
    //----------------------------change direction-------------------------------
    threads.clear();
    i = 0;
	for (; i < thread_num - 1; i++) {
        if(i >= refpos_snp_map.size() || i >= querypos_snp_map.size()){
            dout << "[Error] index out of map range" << endl;
            continue;
        }
        if(refpos_snp_map[i].size() == 0 || querypos_snp_map[i].size() == 0){
            continue;
        }
		//threads.push_back(thread(f));
        //dout << "create new thread" << endl;
        threads.push_back(thread(&VCF::ComplexSearchInThread, this, ref(querypos_snp_map[i]),
                    ref(refpos_snp_map[i])));
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
    if(i >= refpos_snp_map.size() || i >= querypos_snp_map.size()){
        dout << "[Error] index out of map range" << endl;
    }else if(refpos_snp_map[i].size() != 0 && querypos_snp_map[i].size() != 0){
	    ComplexSearchInThread(querypos_snp_map[i], refpos_snp_map[i]);
    }

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    //[todo] check boundries
    //-----------------------------------------------------------------------------

}

bool VCF::CheckTandemRepeat(string sequence, int unit_threshold) {
	int sequence_length = (int)sequence.length();
    if(sequence_length == 1) return true;
    //if(sequence_length > MAX_REPEAT_LEN) return false;
	
    // transform to upper before checking tandem repeat
	transform(sequence.begin(), sequence.end(), sequence.begin(), ::toupper);
    int end_index = sequence_length / 2 + 1;
	bool final_checking = false;
    int repeat_threshold = min(end_index-1, unit_threshold);
	for (int repeat_length = 1; repeat_length <= end_index; repeat_length++) {
		bool is_tandem_repeat = true;
        int repeat_time = 1;
		string repeat_region = sequence.substr(0, repeat_length);
		int start_position = repeat_length;
		while (start_position < sequence_length) {
			if (start_position + repeat_length > sequence_length)
				break;
			string matching_region = sequence.substr(start_position, repeat_length);
			if (matching_region != repeat_region) {
				is_tandem_repeat = false;
				break;
			}
			start_position += repeat_length;
            repeat_time ++;
		}
		if (is_tandem_repeat && repeat_time > 1) {
            //if(debug_f == -1){
            //    dout << sequence << endl;
			//    dout << repeat_region << endl;
            //}
            final_checking = true;
			break;
		}
    }

	return final_checking;
}

/*
	clustering snps
	algorithm description, please refer to paper method
*/
void VCF::ClusteringSnps() {
	//int num = 0;
	for (int i = 0; i < refpos_2_snp.size(); i++) {
		auto & m = refpos_2_snp[i];
		for (auto it = m.begin(); it != m.end(); ++it) {
			auto & v = it->second;
			for (int k = 0; k < v.size(); k++) {
				if (v[k].flag != 1) {
					v[k].flag = 1;
				}
				data_list.push_back(v[k]);
				//num ++;
			}
		}
	}
	//dout << "ref num: " << num << endl;

	//num = 0;
	for (int i = 0; i < querypos_2_snp.size(); i++) {
		auto & m = querypos_2_snp[i];
		for (auto it = m.begin(); it != m.end(); ++it) {
			auto & v = it->second;
			for (int k = 0; k < v.size(); k++) {
				v[k].flag = -1;
				data_list.push_back(v[k]);
				//num ++;
			}
		}
	}

	//dout << "que num: " << num << endl;

	if (data_list.size() == 0)
		return;

	sort(data_list.begin(), data_list.end());

	int cluster_index = 0;
	int ins_ref = 0;
	int del_ref = 0;
	int ins_que = 0;
	int del_que = 0;
	int c_start = 0;
    int c_end = 0;
	
    for (int i = 0; i < data_list.size(); i++) {
        auto snp = data_list[i];
		// check if need to separator clusters
        if (i > 0) {
			c_end = snp.pos;
            //dout << snp.flag << "\t" << snp.pos << "\t" << snp.ref << "\t" << snp.alt << endl;
            //dout << c_end << "," << c_start << endl;
            if(c_end-c_start >= 2){
                string separator = genome_sequence.substr(c_start, c_end - c_start);
                int max_change = max(ins_ref + del_que, ins_que + del_ref);
                //dout << "@" << separator.length() << "\t" << 2* max_change << "\t" << MAX_REPEAT_LEN << endl;
                if ((int)(separator.length()) > 2 * max_change &&
                    ((int)(separator.length()) > MAX_REPEAT_LEN || !CheckTandemRepeat(separator, max_change))) 
                {
                    cluster_index++;
                    ins_ref = 0;
                    del_ref = 0;
                    ins_que = 0;
                    del_que = 0;
                    c_start = 0; // re-assign c_start
                }
            }
		}
        //if(snp.pos == 167582762 || snp.pos == 167582766){
        //if(cluster_index == 30166){
        //    dout << snp.flag << "\t" << snp.pos << "\t" << snp.ref << "\t" << snp.alt << endl;
        //    dout << c_end << "," << c_start << endl;
        //    dout << cluster_index << endl;
        //}

        if(c_start < snp.pos + (int)(snp.ref.length())) c_start = snp.pos + (int)(snp.ref.length());
		
        // assign snp to cluster
		cluster_snps_map[cluster_index].push_back(snp);
		
		//dout << i << ":" << cluster_index << endl;
		int ref_length = (int)(snp.ref.length());
		int alt_length = (int)(snp.alt.length());
		int diff_length = abs(ref_length - alt_length);
		if (snp.flag == 1) {
			if (snp.snp_type == 'I') {
				ins_ref += diff_length;
			}
			else if (snp.snp_type == 'D') {
				del_ref += diff_length;
			}
		}
		else {
			if (snp.snp_type == 'I') {
				ins_que += diff_length;
			}
			else if (snp.snp_type == 'D') {
				del_que += diff_length;
			}
		}
	}
    //cout << cluster_index << " clusters" << endl;
}

/* 
	clustering snps
	old algorithm, fixed length bound and threshold
	expected to have almost the same result but maybe worse than new algorithm
	keep old algorithm for testing
 */
void VCF::ClusteringSnpsOldAlgorithm(int threshold, int lower_bound) {
	for (int i = 0; i < refpos_2_snp.size(); i++) {
		auto & m = refpos_2_snp[i];
		for (auto it = m.begin(); it != m.end(); ++it) {
			auto & v = it->second;
			for (int k = 0; k < v.size(); k++) {
				if (v[k].flag != 1) {
					v[k].flag = 1;
				}
				data_list.push_back(v[k]);
			}
		}
	}
	for (int i = 0; i < querypos_2_snp.size(); i++) {
		auto & m = querypos_2_snp[i];
		for (auto it = m.begin(); it != m.end(); ++it) {
			auto & v = it->second;
			for (int k = 0; k < v.size(); k++) {
				v[k].flag = -1;
				data_list.push_back(v[k]);
			}
		}
	}
	if (data_list.size() == 0)
		return;
    sort(data_list.begin(), data_list.end());

	int cluster_index = 0;
	int previous_data = 0;

	for (int i = 0; i < data_list.size(); i++) {
		auto snp = data_list[i];
		int pos = snp.pos;
		int distance = pos - previous_data;
		if (distance > threshold) {
			cluster_index ++;
		}
		else {
			if (distance > lower_bound) {
				string subsequence = genome_sequence.substr(previous_data, snp.pos - previous_data);
				if (!CheckTandemRepeat(subsequence,(int)(subsequence.length())))
					cluster_index++;
			}
		}
		cluster_snps_map[cluster_index].push_back(snp);
        int current_data = snp.pos + (int)(snp.ref.length());
		if (previous_data < current_data)
			previous_data = current_data;
	}
    dout << cluster_index << " clusters" << endl;
}

bool VCF::MatchSnpLists(vector<SNP> & ref_snp_list,
                        vector<SNP> & query_snp_list,
                        vector<SNP> & mixed_list,
                        const string subsequence,
                        int offset,
                        int thread_index)
{
    //dout << "try match" << endl;
	map<string, vector<SNP> > ref_choice_snps;
	sort(mixed_list.begin(), mixed_list.end());

    //if(debug_f == -1){
    //    dout << "ref:" << endl;
    //}
	for (int i = ref_snp_list.size(); i >= 1; i--) {
		vector<vector<SNP> > combinations = CreateCombinations(ref_snp_list, i);
		//dout << ref_snp_list.size() << "\t" << i << "\t" << combinations.size() << endl;
        for (int k = 0; k < combinations.size(); k++) {
			auto c = combinations[k];
			if(CheckVariantOverlap(c)) continue;
            
            //dout << subsequence << endl;
            //if(debug_f == -1){
            //for(int j = 0; j < c.size(); j++){
            //    dout << c[j].flag << "," << c[j].pos << "," << c[j].ref << "," << c[j].alt << ":";
            //}
            //}
            //dout << offset << endl;
			
            string ref_sequence = ModifySequenceBySnpList(subsequence, c, offset);
			//if(debug_f == -1){
            //    dout << ref_sequence << endl;
            //}
            ref_choice_snps[ref_sequence] = c;
		}
	}

    //dout << "hello world" << endl;
    if(debug_f == -1){
        dout << "que:" << endl;
    }
    //dout << '@' << query_snp_list.size() << endl;
	for (int i = query_snp_list.size(); i >= 1; i--) {
		vector<vector<SNP> > combinations = CreateCombinations(query_snp_list, i);
		for (int k = 0; k < combinations.size(); k++) {
			auto c = combinations[k];
		    //dout  << "@" << i << "\t" << combinations.size() << endl;
			if(CheckVariantOverlap(c)) continue;
            string que_sequence = ModifySequenceBySnpList(subsequence, c, offset);
            //if(debug_f == -1){
            //for(int j = 0; j < c.size(); j++){
            //    dout << c[j].flag << "," << c[j].pos << "," << c[j].ref << "," << c[j].alt << ":";
            //}
            //dout << que_sequence << endl;
            //}
			if (ref_choice_snps.find(que_sequence) != ref_choice_snps.end()) {
				// delete all matched
                auto r = ref_choice_snps[que_sequence];
				//dout << "$$matched: " << c.size() << "\t" << r.size() << endl;
				sort(r.begin(), r.end());
                //if(debug_f == -1){
                //    dout << "#match" << endl;
                //}
                //dout << "ref:";

                //[todo] here we need creating record and insert it into the vector
                string matching_result = "";
                matching_result += chromosome_name;
                
                string parsimonious_ref = subsequence;
                string parsimonious_alt = que_sequence;
                if(parsimonious_ref == parsimonious_alt){
                    dout << "[Error] in variant, ref == alt";
                }
                int min_parsimonious_len = min(parsimonious_ref.size(), parsimonious_alt.size());
                int chop_left = 0;
                int chop_right = 0;
                for(int i = 0; i < min_parsimonious_len; i++){
                    if(toupper(parsimonious_ref[i]) == toupper(parsimonious_alt[i])){
                        chop_left ++;
                    }else{
                        break;
                    }
                }
                for(int i = min_parsimonious_len-1; i >= 0; i--){
                    if(toupper(parsimonious_ref[i]) == toupper(parsimonious_alt[i])){
                        chop_right ++;
                    }else{
                        break;
                    }
                }
                // 1-based
                matching_result += "\t" + to_string(chop_left + offset + 1);

                parsimonious_ref = parsimonious_ref.substr(chop_left, (int)(parsimonious_ref.length()) - chop_left - chop_right);
                parsimonious_alt = parsimonious_alt.substr(chop_left, (int)(parsimonious_alt.length()) - chop_left - chop_right);
                matching_result += "\t" + parsimonious_ref + "\t" + parsimonious_alt;

                string ref_matching_variants = "";

				for (int m = 0; m < r.size(); m++) {
					SNP r_snp = r[m];
					for (auto n = mixed_list.begin(); n != mixed_list.end(); n++) {
						SNP m_snp = *n;
						if (m_snp.pos == r_snp.pos &&
                            m_snp.ref == r_snp.ref &&
                            m_snp.alt == r_snp.alt &&
                            m_snp.flag == r_snp.flag)
                        {
                            //dout << m_snp.pos << "," << m_snp.ref << "," << m_snp.alt << "," << m_snp.flag << ":";
							mixed_list.erase(n);
                            break;
                        }
					}
                    for (auto n = ref_snp_list.begin(); n != ref_snp_list.end(); n++){
                        SNP m_snp = *n;
						if (m_snp.pos == r_snp.pos &&
                            m_snp.ref == r_snp.ref &&
                            m_snp.alt == r_snp.alt &&
                            m_snp.flag == r_snp.flag)
                        {
                        	// 1-based
                            ref_matching_variants += to_string(m_snp.pos+1) + "," + m_snp.ref + "," + m_snp.alt + ";";
							ref_snp_list.erase(n);
                            break;
                        }
                    }
				}
                matching_result += "\t" + ref_matching_variants;
                //dout << endl;
                //dout << "que:";
                string que_matching_variants = "";
				sort(c.begin(), c.end());
				for (int m = 0; m < c.size(); m++) {
					SNP q_snp = c[m];
					for (auto n = mixed_list.begin(); n != mixed_list.end(); n++) {
						SNP m_snp = *n;
						if (m_snp.pos == q_snp.pos &&
                            m_snp.ref == q_snp.ref &&
                            m_snp.alt == q_snp.alt &&
                            m_snp.flag == q_snp.flag)
                        {
                            //dout << m_snp.pos << "," << m_snp.ref << "," << m_snp.alt << "," << m_snp.flag << ":";
							mixed_list.erase(n);
                            break;
                        }
					}
                    for (auto n = query_snp_list.begin(); n != query_snp_list.end(); n++){
                        SNP m_snp = *n;
						if (m_snp.pos == q_snp.pos &&
                            m_snp.ref == q_snp.ref &&
                            m_snp.alt == q_snp.alt &&
                            m_snp.flag == q_snp.flag)
                        {
                        	// 1-based
                            que_matching_variants += to_string(m_snp.pos+1) + "," + m_snp.ref + "," + m_snp.alt + ";";
							query_snp_list.erase(n);
                            break;
                        }
                    }
				}
                matching_result += "\t" + que_matching_variants + "\n";
                if(thread_num == 1){
                    std::lock_guard<std::mutex> guard(complex_match_mutex);
                    complex_match_records[thread_index].push_back(matching_result);
                }else{
                    complex_match_records[thread_index].push_back(matching_result);
                }
                
                //dout << endl;
				//[todo] maybe multi-matching are in one cluster, should check left variants
				//dout << "matched" << endl;
                return true;
			}
		}
	}
	return false;
}

void VCF::ClusteringSearchInThread(int start, int end, int thread_index) {
    for (int cluster_id = start; cluster_id < end; cluster_id++) {
		if (cluster_snps_map.find(cluster_id) != cluster_snps_map.end()) {
		    //dout << "cluster " << cluster_id << endl;	
            auto & snp_list = cluster_snps_map[cluster_id];
            vector<SNP> candidate_ref_snps;
			vector<SNP> candidate_que_snps;
			vector<SNP> candidate_snps;
            int min_pos = std::numeric_limits<int>::max();
			int max_pos = 0;
			for (int i = 0; i < snp_list.size(); i++) {
				auto s = snp_list[i];
				if (s.flag == 1) {
					candidate_ref_snps.push_back(s);
				}
				else if(s.flag == -1) {
					candidate_que_snps.push_back(s);
				}
                candidate_snps.push_back(s);
				if (min_pos > s.pos) min_pos = s.pos;
				if (max_pos < s.pos + (int)(s.ref.length())) max_pos = s.pos + (int)(s.ref.length());
			}

			min_pos = max(0, min_pos - 1);
			max_pos = min(max_pos + 1, (int)genome_sequence.length());
            string subsequence = genome_sequence.substr(min_pos, max_pos-min_pos);

            if (candidate_ref_snps.size() == 0 || candidate_que_snps.size() == 0) continue;
			if (candidate_ref_snps.size() <= 1 && candidate_que_snps.size() <= 1) continue;
			//dout << cluster_id << " before matching: " << snp_list.size() << endl;
			//dout << candidate_ref_snps.size() << "\t" << candidate_que_snps.size() << endl;
		    //if(cluster_id == 30166){
            //    debug_f = -1;
            //}else{
            //    debug_f = 1;
            //} 
            
            if(candidate_ref_snps.size() > 10 || candidate_que_snps.size() > 10){
                //need re-clustering
                //dout << "re-clustering" << endl;
                vector<SNP> cluster_ref_snps;
                vector<SNP> cluster_que_snps;
                int ins_ref = 0;
                int del_ref = 0;
                int ins_que = 0;
                int del_que = 0;
                int c_start = std::numeric_limits<int>::max();
                int c_end = std::numeric_limits<int>::max();
                for(int i = 0; i < candidate_snps.size(); i++){
                    candidate_snps[i].pos += (int)candidate_snps[i].ref.length();
                }
                
                sort(candidate_snps.begin(), candidate_snps.end());

                for (int i = candidate_snps.size()-1; i >= 0; i--) {
                    auto snp = candidate_snps[i];
                    //dout << snp.flag << "\t" << snp.pos << "\t" << snp.ref << "\t" << snp.alt << "\t" << cluster_id << endl;
                    // check if need to separator clusters
                    if (i < candidate_snps.size() - 1) {
                        int c_start = snp.pos;
                        //dout << c_start << "," << c_end << endl;
                        if(c_start < c_end){
                            string separator = genome_sequence.substr(c_start, c_end - c_start);
                            int max_change = max(ins_ref + del_que, ins_que + del_ref);
                            //dout << "$" << ins_ref << "," << del_que << "," << ins_que << "," << del_ref << endl;
                            //dout << "@" << separator.length() << "\t" << 2* max_change << "\t" << MAX_REPEAT_LEN << endl;
                            if ((int)separator.length() > 2 * max_change && !CheckTandemRepeat(separator, max_change)) 
                            {
                                //if(cluster_ref_snps.size() > 0 && cluster_que_snps.size() > 0){
                                //dout << "=========================================" << endl;    
                                while(cluster_ref_snps.size() > 0 &&
                                        cluster_que_snps.size() > 0 &&
                                        MatchSnpLists(cluster_ref_snps, cluster_que_snps, snp_list, subsequence, min_pos, thread_index));
                                //}
                                //dout << "find breaking" << endl;
                                cluster_ref_snps.clear();
                                cluster_que_snps.clear();
                                ins_ref = 0;
                                del_ref = 0;
                                ins_que = 0;
                                del_que = 0;
                            }
                            //if(separator.length() > 2*max_change && CheckTandemRepeat(separator, max_change)){
                                //dout << separator << endl;
                            //}
                        }
                    }

                    if(c_end > snp.pos- (int)snp.ref.length()) c_end = snp.pos - (int)snp.ref.length();
                    // assign snp to cluster
                    snp.pos -= (int)snp.ref.length();
                    if(snp.flag == 1){
                        cluster_ref_snps.push_back(snp);
                    }else{
                        cluster_que_snps.push_back(snp);
                    }
                    //cluster_snps_map[cluster_index].push_back(snp);
                    
                    //dout << i << ":" << cluster_index << endl;
                    int ref_length = (int)snp.ref.length();
                    int alt_length = (int)snp.alt.length();
                    int diff_length = abs(ref_length - alt_length);
                    if (snp.flag == 1) {
                        if (snp.snp_type == 'I') {
                            ins_ref += diff_length;
                        }
                        else if (snp.snp_type == 'D') {
                            del_ref += diff_length;
                        }
                    }
                    else {
                        if (snp.snp_type == 'I') {
                            ins_que += diff_length;
                        }
                        else if (snp.snp_type == 'D') {
                            del_que += diff_length;
                        }
                    }
                }
                
                //if separating cluster does not work, try heuristic, if still not work, discard this cluster
                if(cluster_ref_snps.size() > 20 || cluster_que_snps.size() > 20){
                    
                    // final check by variant length, if not applicable, skip it and give a warning.
                    if (cluster_ref_snps.size() > cluster_que_snps.size()){

                        int ref_sum_del_len = 0;
                        int ref_sum_ins_len = 0;
                        for(int j = 0; j < cluster_ref_snps.size(); j++){
                            int len_change = cluster_ref_snps[j].ref.size() -  cluster_ref_snps[j].alt.size();
                            if (len_change > 0){
                                ref_sum_del_len += len_change;
                            }else if(len_change < 0){
                                ref_sum_ins_len -= len_change;
                            }
                        }
                        bool skip_flag = false;
                        for(int j = 0; j < cluster_que_snps.size(); j++){
                            int len_change = cluster_que_snps[j].ref.size() - cluster_que_snps[j].alt.size();
                            if(len_change > 0){
                                if (ref_sum_del_len < len_change){
                                    skip_flag = true;
                                    break;
                                }
                            }else if(len_change < 0){
                                if (ref_sum_ins_len < len_change * -1){
                                    skip_flag = true;
                                    break;
                                }
                            }
                        }
                        if (skip_flag) continue;

                    }else{
                    
                        int que_sum_del_len = 0;
                        int que_sum_ins_len = 0;
                        for(int j = 0; j < cluster_que_snps.size(); j++){
                            int len_change = cluster_que_snps[j].ref.size() -  cluster_que_snps[j].alt.size();
                            if (len_change > 0){
                                que_sum_del_len += len_change;
                            }else if(len_change < 0){
                                que_sum_ins_len -= len_change;
                            }
                        }
                        bool skip_flag = false;
                        for(int j = 0; j < cluster_ref_snps.size(); j++){
                            int len_change = cluster_ref_snps[j].ref.size() - cluster_ref_snps[j].alt.size();
                            if(len_change > 0){
                                if (que_sum_del_len < len_change){
                                    skip_flag = true;
                                    break;
                                }
                            }else if(len_change < 0){
                                if (que_sum_ins_len < len_change * -1){
                                    skip_flag = true;
                                    break;
                                }
                            }
                        }
                        if(skip_flag) continue;
                    
                    }

                    cout << "[Warning] large cluster found, skip it." << endl;
                    //dout << "ref snps:" << endl;
                    //for(int j = 0; j < cluster_ref_snps.size(); j++){
                    //    dout << cluster_ref_snps[j].flag << "," << cluster_ref_snps[j].pos << "," << cluster_ref_snps[j].ref << "," << cluster_ref_snps[j].alt << endl;
                    //}
                    //dout << "alt snps:" << endl;
                    //for(int j = 0; j < cluster_que_snps.size(); j++){
                    //    dout << cluster_que_snps[j].flag << "," << cluster_que_snps[j].pos << "," << cluster_que_snps[j].ref << "," << cluster_que_snps[j].alt << endl;
                    //}
                    continue;
                }

                while(cluster_ref_snps.size() > 0 &&
                        cluster_que_snps.size() > 0 &&
                        MatchSnpLists(cluster_ref_snps, cluster_que_snps, snp_list, subsequence, min_pos, thread_index));

            }
            else
            {

                while(candidate_ref_snps.size() > 0 &&
                        candidate_que_snps.size() > 0 &&
                        MatchSnpLists(candidate_ref_snps, candidate_que_snps, snp_list, subsequence, min_pos, thread_index));
                //MatchSnpLists(candidate_ref_snps, candidate_que_snps, snp_list, subsequence, min_pos);
            }
            //dout << "after matching" << snp_list.size() << "," << cluster_snps_map[cluster_id].size() << endl;
		}
		else {
			break;
		}
	}
}

// match by cluster
void VCF::ClusteringSearchMultiThread() {

	
	clustering_search = true;
	int start = cluster_snps_map.begin()->first;
	int cluster_number = cluster_snps_map.size();
	int cluster_end_boundary = start + cluster_number;
	int cluster_step = cluster_number / thread_num;
	if (cluster_step * thread_num < cluster_number) cluster_step++;
	int end = start + cluster_step;
	
    //initialize vector size, each allocating will have a lock
    vector<string> temp_vector;
    for(int j = 0; j < thread_num; j++){
        complex_match_records.push_back(temp_vector);
    }

	vector<thread> threads;
	//spawn threads
	unsigned i = 0;
	for (; i < thread_num - 1; i++) {
		//threads.push_back(thread(f));
		//dout << "create new thread" << endl;
        int variant_number = 0;
        for (int cluster_id = start; cluster_id < end; cluster_id++) {
		    if (cluster_snps_map.find(cluster_id) != cluster_snps_map.end()) {
                variant_number += cluster_snps_map[cluster_id].size();
            }
        }
		threads.push_back(thread(&VCF::ClusteringSearchInThread, this, start, end, i));
		start = end;
		end = start + cluster_step;
	}
	// also you need to do a job in main thread
	// i equals to (thread_num - 1)
	if (i != thread_num - 1) {
		dout << "[Error] thread number not match" << endl;
	}
	if (start >= cluster_snps_map.size()) {
		dout << "[Error] index out of map range" << endl;
	}
	else {
        int variant_number = 0;
        for (int cluster_id = start; cluster_id < end; cluster_id++) {
		    if (cluster_snps_map.find(cluster_id) != cluster_snps_map.end()) {
                variant_number += cluster_snps_map[cluster_id].size();
            }
        }
		ClusteringSearchInThread(start, end, i);
	}

	// call join() on each thread in turn before this function?
	std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
}

int VCF::GetRefSnpNumber() {
	int result = 0;
	if (clustering_search) {
		for (auto it = cluster_snps_map.begin(); it != cluster_snps_map.end(); it++) {
			auto v = it->second;
			for (int i = 0; i < v.size(); i++) {
				if (v[i].flag == 1)
					result++;
			}
		}
	}
    else if(complex_search){
        for (int i = 0; i < refpos_snp_map.size(); i++){
			//[todo] correct this
            result += refpos_snp_map[i].size();
        }
    }else{
	    for (int i = 0; i < refpos_2_snp.size(); i++) {
		    result += refpos_2_snp[i].size();
	    }
    }
	return result;
}

int VCF::GetQuerySnpNumber() {
	int result = 0;
	if (clustering_search) {
		for (auto it = cluster_snps_map.begin(); it != cluster_snps_map.end(); it++) {
			auto v = it->second;
			for (int i = 0; i < v.size(); i++) {
				if (v[i].flag == -1)
					result++;
			}
		}
	}
    else if(complex_search){
        for(int i = 0; i < querypos_snp_map.size(); i++){
			//[todo] correct this
            result += querypos_snp_map[i].size();
        }
    }else{
	    for (int i = 0; i < querypos_2_snp.size(); i++) {
		    result += querypos_2_snp[i].size();
	    }   
    }
	return result;
}

void VCF::Compare(string ref_vcf,
        string query_vcf,
        string genome_seq,
        bool direct_search,
        string output_prefix){

	output_stat_filename = output_prefix + ".stat";
    output_simple_filename = output_prefix + ".simple";
    output_complex_filename = output_prefix + ".complex";

    //------------read genome sequence and decide boundary according to thread number
	dsptime();
	dout << " Read genome sequence file... " << endl;
	ReadGenomeSequence(genome_seq);
	dsptime();
	dout << " Finish reading genome sequence file." << endl;
	//------------read ref and query vcf file
	dsptime();
	dout << " Read reference vcf file... " << endl;
	ReadRefVCF(ref_vcf);
	dsptime();
	dout << " Read query vcf file... " << endl;
	ReadQueryVCF(query_vcf);
	dsptime();
	dout << " Finish reading all vcf file." << endl;

	//------------check vcf entry number before matching
	int ref_total_num = GetRefSnpNumber();
    int que_total_num = GetQuerySnpNumber();
    dout << " referece vcf entry number: " << ref_total_num << endl;
	dout << " query vcf entry number: " << que_total_num << endl;


	//------------direct search
	dsptime();
	dout << " Direct search ... " << endl;
	DirectSearchMultiThread();
	dsptime();
	dout << " Finish direct search." << endl;
    int ref_direct_left_num = GetRefSnpNumber();
    int que_direct_left_num = GetQuerySnpNumber();
    int ref_direct_match_num = ref_total_num - ref_direct_left_num;
    int que_direct_match_num = que_total_num - que_direct_left_num;
	dout << " referece vcf entry direct match number: " << ref_direct_match_num << endl;
	dout << " query vcf entry direct match number: " << que_direct_match_num  << endl;

	if (direct_search){
	    dout << " referece vcf entry mismatch number: " << ref_direct_left_num << endl;
	    dout << " query vcf entry mismatch number: " << que_direct_left_num  << endl;
        ofstream output_stat_file;
        output_stat_file.open(output_stat_filename);
        output_stat_file << ref_total_num << endl;
        output_stat_file << que_total_num << endl;
        output_stat_file << ref_direct_match_num << endl;
        output_stat_file << que_direct_match_num << endl;
        output_stat_file << ref_direct_left_num << endl;
        output_stat_file << que_direct_left_num << endl;
        output_stat_file.close();

        return;
    }

	//------------complex search
	//dsptime();
	//dout << " Complex search ... " << endl;
	//ComplexSearchMultiThread();
	//dsptime();
	//dout << " Finish complex search." << endl;
	//dout << " referece vcf entry number: " << GetRefSnpNumber() << endl;
	//dout << " query vcf entry number: " << GetQuerySnpNumber() << endl;

	//-------------clustering search
	dsptime();
	dout << " Clustering snps ... " << endl;
	ClusteringSnps();
    //ClusteringSnpsOldAlgorithm(400, 10);
	dsptime();
	dout << " Finish clustering." << endl;
	dsptime();
	dout << " Clustering search ... " << endl;
	ClusteringSearchMultiThread();

    ofstream output_complex_file;
    output_complex_file.open(output_complex_filename);
    output_complex_file << "##VCF1:" << ref_vcf << endl;
    output_complex_file << "##VCF2:" << query_vcf << endl;
    output_complex_file << "#CHR\tPOS\tREF\tALT\tVCF1\tVCF2" << endl;
    for(int i = 0; i < complex_match_records.size(); i++){
        for (int j = 0; j < complex_match_records[i].size(); j++){
            if(complex_match_records[i][j].find_first_not_of(' ') != std::string::npos){
                output_complex_file << complex_match_records[i][j];
            }
        }
    }
    output_complex_file.close();
    complex_match_records.clear();

	dsptime();
	dout << " Finish clustering search." << endl;
	int ref_cluster_left_num = GetRefSnpNumber();
    int que_cluster_left_num = GetQuerySnpNumber();
    int ref_cluster_match_num = ref_direct_left_num - ref_cluster_left_num;
    int que_cluster_match_num = que_direct_left_num - que_cluster_left_num;

    dout << " referece vcf entry cluster match number: " << ref_cluster_match_num << endl;
	dout << " query vcf entry cluster match number: " << que_cluster_match_num << endl;

	dout << " referece vcf entry mismatch number: " << ref_cluster_left_num << endl;
	dout << " query vcf entry mismatch number: " << que_cluster_left_num  << endl;
    
    //write stat file
    ofstream output_stat_file;
    output_stat_file.open(output_stat_filename);
    output_stat_file << ref_total_num << endl;
    output_stat_file << que_total_num << endl;
    output_stat_file << ref_direct_match_num << endl;
    output_stat_file << que_direct_match_num << endl;
    output_stat_file << ref_cluster_match_num << endl;
    output_stat_file << que_cluster_match_num << endl;
    output_stat_file << ref_cluster_left_num << endl;
    output_stat_file << que_cluster_left_num << endl;
    output_stat_file.close();

    return;
}
