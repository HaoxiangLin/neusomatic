/** 
  * File Name :
  * Purpose :
  * Creation Date : 28-08-2017
  * Last Modified :
  * Created By : Mohammad Sahraeian  
  */





#include "SeqLib/BamReader.h"
#include "SeqLib/RefGenome.h"

#ifdef BGZF_MAX_BLOCK_SIZE
#pragma push_macro("BGZF_MAX_BLOCK_SIZE")
#undef BGZF_MAX_BLOCK_SIZE
#define BGZF_MAX_BLOCK_SIZE_BAK
#endif

#ifdef BGZF_BLOCK_SIZE
#pragma push_macro("BGZF_BLOCK_SIZE")
#undef BGZF_BLOCK_SIZE
#define BGZF_BLOCK_SIZE_BAK
#endif



#include "vcf.h"
#include "bedio.hpp"
#include "targeted_loading.hpp"
#include "msa.hpp"
#include "msa_utils.hpp"
#include "Options.h"

#ifdef BGZF_MAX_BLOCK_SIZE_BAK
#undef BGZF_MAX_BLOCK_SIZE_BAK
#pragma pop_macro("BGZF_MAX_BLOCK_SIZE")
#endif

#ifdef BGZF_BLOCK_SIZE_BAK
#undef BGZF_BLOCK_SIZE_BAK
#pragma pop_macro("BGZF_BLOCK_SIZE")
#endif


int main(int argc, char **argv) {
  neusomatic::Options opts(argc, argv);
  const std::string& bam_path = opts.bam_in();
  const std::string& bed_in = opts.target_region_in();
  const std::string& ref_in = opts.ref();
  const std::string& vcf_out = opts.vcf_out();
  const std::string& count_out = opts.count_out();
  int window_size = opts.window_size();
  float min_af = opts.min_allele_freq();
  float ins_min_af=min_af;
  float del_min_af=min_af;
  float snp_min_af=min_af;
  const bool calculate_qual_stat = opts.calculate_qual_stat(); 

  //const std::map<char, int> empty_pileup_counts = {{'-', 0}, {'A', 0}, {'C', 0}, {'G', 0}, {'T', 0}};
  static const std::vector<char> nuc_code_char = {'A', 'C', 'G', 'T', '-', 'N'};

  using GInvStdString = neusomatic::bio::GenomicInterval<std::string>;
  using GInvInt = neusomatic::bio::GenomicInterval<int>;
  using MSA = typename neusomatic::MSABuilder<SeqLib::BamRecord, neusomatic::bio::Variant<std::string, int>>;
  using ContigGaps = typename MSA::ContigGaps;
  neusomatic::BedIO<GInvStdString> bed_reader(bed_in);
  std::vector<GInvStdString> bed_regions = bed_reader.ReadBed3_windowed(window_size);
  SeqLib::RefGenome ref_seqs;
  ref_seqs.LoadIndex(ref_in);

  neusomatic::bio::VCFWriter vcf_writer(vcf_out, ref_in, "VarCal");
  const unsigned contig_counts = seqan::length(seqan::contigNames(vcf_writer.vcf_context()));
  std::map<std::string, unsigned> chr_name_to_idx;
  for (unsigned i = 0; i < contig_counts; ++i) {
    chr_name_to_idx[seqan::toCString(seqan::contigNames(vcf_writer.vcf_context())[i])] = i; 
  }
  std::ofstream count_out_writer;
  count_out_writer.open(count_out);

  neusomatic::CaptureLayout<GInvInt, SeqLib::BamRecord, SeqLib::BamReader> capture_layout(bam_path, bed_regions, opts);
  SeqLib::BamHeader bam_header = capture_layout.Reader().Header(); 
  
  try {
    for (auto it = chr_name_to_idx.cbegin(); it != chr_name_to_idx.cend(); ++it) {
      if (bam_header.Name2ID(it->first) != it->second) {
        std::cerr << " the bam file and the reference do not match.\n exit..\n please check the bam header and the reference file.\n";
        exit(1);
      }
    }
  } catch (const std::out_of_range& oor) {
        std::cerr << " the reference contains chromosome/contig name(s) that is/are not in the bam file.\n exit..\n please check the bam header and the reference file.\n";
    exit(1);
  } catch (const std::invalid_argument& ia) {
        std::cerr << " the reference contains chromosome/contig name(s) that is/are not in the bam file.\n exit..\n please check the bam header and the reference file.\n";
    exit(1);
  }

  int cnt_region=0;
  while (capture_layout.NextRegion(opts.fully_contained())) {
    // a map from genomic interval -> a vector of alignReadIds
    for (auto targeted_region : capture_layout.ginv_records()) {
      auto ginv = targeted_region.first;
      auto& records = targeted_region.second;
      GInvStdString ginvstr(bam_header.IDtoName(ginv.contig()), ginv.left(), ginv.right());
      if (opts.verbosity() > 2 || cnt_region % 100 == 0){
        std::cerr<<"#On region "<<ginvstr<<"\n";
        std::cerr<<"#Aligned read number: "<<records.size()<<std::endl;
      }
      ++cnt_region;
      if (records.empty()) continue; 
      if (records.size() > opts.max_depth()) {
        records.resize(opts.max_depth());
      }
      const auto non_gapped_ref = ref_seqs.QueryRegion(ginvstr.contig(), ginvstr.left(), ginvstr.right() - 1);
      if (ginv.length() > non_gapped_ref.size())  {
        ginv.right() = ginv.left() + non_gapped_ref.size();
      }
      MSA msa(ginv, records, non_gapped_ref);
      std::vector<std::string> msa_, bqual_; 
      std::vector<std::vector<int>> lscs_, rscs_;
      std::vector<int> mqual_;
      std::vector<int> strand_;
      std::vector<std::vector<int>> tag_;
      if (calculate_qual_stat){
        std::tie(msa_,bqual_,mqual_,strand_,lscs_,rscs_,tag_)=msa.GetMSAwithQual();
      }else{
        msa_ = msa.GetMSA();
      }
      if (opts.verbosity()>0){
        for (const auto& l : msa_) {
          std::cout << l << std::endl;
        }
        if (calculate_qual_stat){
          for (const auto& l : bqual_) {
            std::cout << l << std::endl;
          }
          for (const auto& l : lscs_) {
            for (const auto& e : l) {
              std::cout << e;
            }
            std::cout << std::endl;
          }
          for (const auto& l : rscs_) {
            for (const auto& e: l) {
              std::cout << e;
            }
            std::cout << std::endl;
          }
          for (const auto& l : mqual_) {
            std::cout << l << std::endl;
          }
          for (const auto& l : strand_) {
            std::cout << l << std::endl;
          }
          for (const auto& l : tag_) {
            for (const auto& ll : l) {
              std::cout << ll << "-";
            }
            std::cout << std::endl;
          }
        }
      }
      auto gapped_ref=msa.ref_gaps();
      auto ref = gapped_ref.to_string();
      if (opts.verbosity()>0){
        std::cout << "Ref: " << std::endl;
        std::cout << ref << std::endl;
      }

      auto condensed_array = calculate_qual_stat ? neusomatic::CreateCondensedArray(msa_, bqual_, mqual_, strand_, lscs_, rscs_, tag_, msa_.size(), ginv, ref, opts.num_threads()) : neusomatic::CreateCondensedArray(msa_, msa_.size(), ginv, ref, opts.num_threads());

      auto cols = condensed_array.GetColSpace();
      auto cols_bqual = condensed_array.GetColSpaceBQual();
      auto cols_mqual = condensed_array.GetColSpaceMQual();
      auto cols_strand = condensed_array.GetColSpaceStrand();
      auto cols_lsc = condensed_array.GetColSpaceLSC();
      auto cols_rsc = condensed_array.GetColSpaceRSC();
      auto cols_tag = condensed_array.GetColSpaceTag();
      auto ncol = cols.size();
      auto cc_ = condensed_array.GetCC();

      for (size_t i = 0; i < ncol; i++) {
        if (ginv.left() + cc_.RefPos(i) >=ginv.right()){ break;}
        auto ref_base = ref[i];
        ref_base = std::toupper(ref_base);
        auto ref_code = neusomatic::CondensedArray<GInvInt,std::string>::DnaCharToDnaCode(ref_base);

        if (ref_base == 'N') {
          ref_base = '-';
        }

        if (opts.verbosity()>0){
          std::cout<<"col "<<i<<": ";
          std::cout<<"(ref= "<< ref_base << ") ";
          for (int base = 0; base < (int) cols[i].base_freq_.size(); ++base) {
            if (calculate_qual_stat){
              std::cout<<"("<<nuc_code_char[base] <<"): "<< cols[i].base_freq_[base] <<","<<int(round(cols[i].bqual_mean[base]))<<","<<int(round(cols_mqual[i].mqual_mean[base]))<< \
              ","<<int(round(cols_strand[i].strand_mean[base]))<<"; ";
            }else{
              std::cout<<"("<<nuc_code_char[base]<<"): "<< cols[i].base_freq_[base] <<"; ";
            }
          }
          std::cout<< std::endl;
        }

        auto nrow = msa_.size()-cols[i].base_freq_[5];
        cols[i].base_freq_.erase(cols[i].base_freq_.begin() + 5);

        std::vector<int> pileup_counts(cols[i].base_freq_.size());
        int total_count=0;
        for (int base = 0; base < (int) cols[i].base_freq_.size(); ++base) {
          pileup_counts[base] = cols[i].base_freq_[base];
          total_count+=cols[i].base_freq_[base];
        }



        if (total_count==0) {continue;}

        int rsc_counts=0;
        int lsc_counts=0;
        auto start_pos=ginv.left() + cc_.RefPos(i);
        if (ref_base!='-'){
          start_pos++;
        }
        if (calculate_qual_stat){ 
          for(auto it = cols_lsc[i].lsc_mean.cbegin(); it != cols_lsc[i].lsc_mean.cend(); ++it) {
            rsc_counts+=*it;
          }
          for(auto it = cols_rsc[i].rsc_mean.cbegin(); it != cols_rsc[i].rsc_mean.cend(); ++it) {
            lsc_counts+=*it;
          }
          int sc_counts=lsc_counts+rsc_counts;
          count_out_writer<<bam_header.IDtoName(ginv.contig())<<"\t"<<start_pos<<"\t" \
          << start_pos+1<<"\t"  << ref_base << "\t" \
          <<pileup_counts[4]<<":"<<pileup_counts[0]<<":"<<pileup_counts[1]<<":"<<pileup_counts[2] \
          <<":"<<pileup_counts[3] \
          <<"\t"<<int(round(cols_bqual[i].bqual_mean[4]))<<":"<<int(round(cols_bqual[i].bqual_mean[0]))<<":"<<int(round(cols_bqual[i].bqual_mean[1])) \
          <<":"<<int(round(cols_bqual[i].bqual_mean[2]))<<":"<<int(round(cols_bqual[i].bqual_mean[3])) \
          <<"\t"<<int(round(cols_mqual[i].mqual_mean[4]))<<":"<<int(round(cols_mqual[i].mqual_mean[0]))<<":"<<int(round(cols_mqual[i].mqual_mean[1])) \
          <<":"<<int(round(cols_mqual[i].mqual_mean[2]))<<":"<<int(round(cols_mqual[i].mqual_mean[3])) \
          <<"\t"<<int(round(cols_strand[i].strand_mean[4]))<<":"<<int(round(cols_strand[i].strand_mean[0]))<<":"<<int(round(cols_strand[i].strand_mean[1])) \
          <<":"<<int(round(cols_strand[i].strand_mean[2]))<<":"<<int(round(cols_strand[i].strand_mean[3]))\
          <<"\t"<<int(round(cols_lsc[i].lsc_mean[4]))<<":"<<int(round(cols_lsc[i].lsc_mean[0]))<<":"<<int(round(cols_lsc[i].lsc_mean[1])) \
          <<":"<<int(round(cols_lsc[i].lsc_mean[2]))<<":"<<int(round(cols_lsc[i].lsc_mean[3]))\
          <<"\t"<<int(round(cols_rsc[i].rsc_mean[4]))<<":"<<int(round(cols_rsc[i].rsc_mean[0]))<<":"<<int(round(cols_rsc[i].rsc_mean[1])) \
          <<":"<<int(round(cols_rsc[i].rsc_mean[2]))<<":"<<int(round(cols_rsc[i].rsc_mean[3]))\
          <<"\t"<<int(round(cols_tag[0][i].tag_mean[4]))<<":"<<int(round(cols_tag[0][i].tag_mean[0]))<<":"<<int(round(cols_tag[0][i].tag_mean[1])) \
          <<":"<<int(round(cols_tag[0][i].tag_mean[2]))<<":"<<int(round(cols_tag[0][i].tag_mean[3]))\
          <<"\t"<<int(round(cols_tag[1][i].tag_mean[4]))<<":"<<int(round(cols_tag[1][i].tag_mean[0]))<<":"<<int(round(cols_tag[1][i].tag_mean[1])) \
          <<":"<<int(round(cols_tag[1][i].tag_mean[2]))<<":"<<int(round(cols_tag[1][i].tag_mean[3]))\
          <<"\t"<<int(round(cols_tag[2][i].tag_mean[4]))<<":"<<int(round(cols_tag[2][i].tag_mean[0]))<<":"<<int(round(cols_tag[2][i].tag_mean[1])) \
          <<":"<<int(round(cols_tag[2][i].tag_mean[2]))<<":"<<int(round(cols_tag[2][i].tag_mean[3]))\
          <<"\t"<<int(round(cols_tag[3][i].tag_mean[4]))<<":"<<int(round(cols_tag[3][i].tag_mean[0]))<<":"<<int(round(cols_tag[3][i].tag_mean[1])) \
          <<":"<<int(round(cols_tag[3][i].tag_mean[2]))<<":"<<int(round(cols_tag[3][i].tag_mean[3]))\
          <<"\t"<<int(round(cols_tag[4][i].tag_mean[4]))<<":"<<int(round(cols_tag[4][i].tag_mean[0]))<<":"<<int(round(cols_tag[4][i].tag_mean[1])) \
          <<":"<<int(round(cols_tag[4][i].tag_mean[2]))<<":"<<int(round(cols_tag[4][i].tag_mean[3]))\
          <<std::endl;
        }else{
          count_out_writer<<bam_header.IDtoName(ginv.contig())<<"\t"<<start_pos<<"\t" \
          << start_pos+1<<"\t"  << ref_base << "\t" \
          <<pileup_counts[4]<<":"<<pileup_counts[0]<<":"<<pileup_counts[1]<<":"<<pileup_counts[2] \
          <<":"<<pileup_counts[3]<<std::endl;
        }

        int major = -1;
        int major_count = 0;
        int minor = -1;
        int minor_count = 0;
        int minor2 = -1;
        int minor2_count = 0;

        for (int row = 0;  row < cols[i].base_freq_.size(); ++row) {
          if (cols[i].base_freq_[row] > major_count) {
            minor2 = minor;
            minor2_count = minor_count;
            minor_count = major_count;
            minor = major;
            major_count = cols[i].base_freq_[row];
            major = row;
          } else if (cols[i].base_freq_[row] > minor_count) {
            minor2 = minor;
            minor2_count = minor_count;
            minor_count = cols[i].base_freq_[row];
            minor = row;
          } else if (cols[i].base_freq_[row] > minor2_count) {
            minor2_count = cols[i].base_freq_[row];
            minor2 = row;
          }
        }

        if (minor != -1 and major != -1){
          if (minor2 != -1 and ref_code == major and minor == 4 and ref_code != 4 ){
            if (minor2_count>0.5*minor_count){
              minor = minor2;
              minor_count = minor2_count;
            }
          }
        }
        auto ref_count = cols[i].base_freq_[ref_code];
        auto var_code = ref_code; 
        int var_count = 0;
        auto af = minor_count/float(major_count+minor_count);
        if (major != ref_code){
          var_code = major;
          var_count = major_count;
        } else if (minor != ref_code and ( (minor == 4 and  af > del_min_af ) or
                                        (minor != 4 and ref_base != '-' and af > snp_min_af ) or
                                        (ref_base =='-' and af > ins_min_af))){
          var_code = minor;
          var_count = minor_count;
        }

        if (var_count > 0) { 

          auto record_info = "AF="+std::to_string((var_count)/float(var_count+ref_count))+";DP="+std::to_string(nrow)+";RO="+std::to_string(ref_count)+";AO="+std::to_string(var_count);
          auto gtinfo = "0/1:"+std::to_string(nrow)+":"+std::to_string(ref_count)+":"+std::to_string(var_count);
          if (calculate_qual_stat){
            record_info += ";ST="+std::to_string(int(round(ref_count*(cols_strand[i].strand_mean[ref_code]/100))))+ \
                           ","+std::to_string(int(round(var_count*(cols_strand[i].strand_mean[var_code]/100))))+ \
                           ";LS="+std::to_string(lsc_counts)+\
                           ";RS="+std::to_string(rsc_counts)+\
                           ";NM="+std::to_string(int(round(cols_tag[0][i].tag_mean[var_code])))+\
                           ";AS="+std::to_string(int(round(cols_tag[1][i].tag_mean[var_code])))+ \
                           ";XS="+std::to_string(int(round(cols_tag[2][i].tag_mean[var_code])))+ \
                           ";PR="+std::to_string(int(round(cols_tag[3][i].tag_mean[var_code])))+ \
                           ";CL="+std::to_string(int(round(cols_tag[4][i].tag_mean[var_code])))+ \
                           ";MQ="+std::to_string(int(round(cols_mqual[i].mqual_mean[var_code])))+ \
                           ";BQ="+std::to_string(int(round(cols_bqual[i].bqual_mean[var_code])));
            gtinfo += ":"+std::to_string(int(round(ref_count*(cols_strand[i].strand_mean[ref_code]/100))))+","+ \
                      std::to_string(int(round(var_count*(cols_strand[i].strand_mean[var_code]/100))))+":"+\
                      std::to_string(lsc_counts)+":"+\
                      std::to_string(rsc_counts)+":"+\
                      std::to_string(int(round(cols_tag[0][i].tag_mean[var_code])))+":"+\
                      std::to_string(int(round(cols_tag[1][i].tag_mean[var_code])))+":"+\
                      std::to_string(int(round(cols_tag[2][i].tag_mean[var_code])))+":"+\
                      std::to_string(int(round(cols_tag[3][i].tag_mean[var_code])))+":"+\
                      std::to_string(int(round(cols_tag[4][i].tag_mean[var_code])))+":"+\
                      std::to_string(int(round(cols_mqual[i].mqual_mean[var_code])))+":"+\
                      std::to_string(int(round(cols_bqual[i].bqual_mean[var_code])));
          }
          auto var_base = nuc_code_char[var_code];  
          if (ref_base == '-') {ref_base = 'N';}
          if (var_base == '-') {var_base = 'N';}
          auto var_ref_pos=ginv.left() + cc_.RefPos(i);
          seqan::VcfRecord record;
          record.rID = ginv.contig();
          record.beginPos = var_ref_pos;
          record.id = ".";
          record.ref = ref_base;
          record.alt = var_base;
          record.qual = 100;
          record.filter = ".";
          record.info = record_info;
          char * info;
          if (calculate_qual_stat){
            record.format = "GT:DP:RO:AO:ST:LS:RS:NM:AS:XS:PR:CL:MQ:BQ";
          }else{
            record.format = "GT:DP:RO:AO";
          }
          appendValue(record.genotypeInfos, gtinfo);
          vcf_writer.Write(record);
          if (opts.verbosity()>0){
            std::cout<<"var: " << i << "," << var_ref_pos << ","<< ref_base << "," << var_base<<","<<nrow<<":"<<ref_count<<":"<<var_count<<std::endl;
            std::cout<<"col "<<i<<": ";
            std::cout<<"(ref= "<< ref_base << ") ";
            for (size_t row = 0; row < cols[i].base_freq_.size(); ++row) {
              std::cout<<"("<<row<<"): "<<cols[i][row]<<",";
            }
            std::cout<< std::endl;
          }
        }

      }
    } //end for
  } //end while
  count_out_writer.close();
  return 0;
}
