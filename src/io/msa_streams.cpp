#include <stdexcept>
#include <algorithm>
#include <map>

#include "file_io.hpp"

using namespace std;

FastaStream& operator>>(FastaStream& stream, MSA& msa)
{
  /* open the file */
  auto file = pll_fasta_open(stream.fname().c_str(), pll_map_fasta);
  if (!file)
    throw runtime_error{"Cannot open file " + stream.fname()};

  char * sequence = NULL;
  char * header = NULL;
  long sequence_length;
  long header_length;
  long sequence_number;

  /* read sequences and make sure they are all of the same length */
  int sites = 0;

  /* read the first sequence separately, so that the MSA object can be constructed */
  pll_fasta_getnext(file, &header, &header_length, &sequence, &sequence_length, &sequence_number);
  sites = sequence_length;

  if (sites == -1 || sites == 0)
    throw runtime_error{"Unable to read MSA file"};

  msa = MSA(sites);

  msa.append(sequence, header);

  free(sequence);
  free(header);

  /* read the rest */
  while (pll_fasta_getnext(file, &header, &header_length, &sequence, &sequence_length, &sequence_number))
  {
    if (sites && sites != sequence_length)
      throw runtime_error{"MSA file does not contain equal size sequences"};

    if (!sites) sites = sequence_length;

    msa.append(sequence, header);
    free(sequence);
    free(header);
  }

  if (pll_errno != PLL_ERROR_FILE_EOF)
    throw runtime_error{"Error while reading file: " +  stream.fname()};

  pll_fasta_close(file);

  return stream;
}

PhylipStream& operator>>(PhylipStream& stream, MSA& msa)
{
  unsigned int sequence_count;
  pll_msa_t * pll_msa = pll_phylip_parse_msa(stream.fname().c_str(), &sequence_count);
  if (pll_msa)
  {
    msa = MSA(pll_msa);
    pll_msa_destroy(pll_msa);
  }
  else
    throw runtime_error{"Unable to parse PHYLIP file: " +  stream.fname()};

  return stream;
}

PhylipStream& operator<<(PhylipStream& stream, MSA& msa)
{
  auto retval = pllmod_msa_save_phylip(msa.pll_msa(), stream.fname().c_str());

  if (!retval)
    throw runtime_error{pll_errmsg};

  return stream;
}

PhylipStream& operator<<(PhylipStream& stream, PartitionedMSA& msa)
{
  ofstream fs(stream.fname());

  auto taxa = msa.full_msa().size();
  auto sites = msa.total_length();
  fs << taxa << " " << sites << endl;

  for (size_t i = 0; i < taxa; ++i)
  {
    fs << msa.full_msa().label(i) << " ";
    for (const auto& p: msa.part_list())
    {
      fs << p.msa().at(i);
    }
    fs << endl;
  }

  return stream;
}


CATGStream& operator>>(CATGStream& stream, MSA& msa)
{
  ifstream fs;
  fs.open(stream.fname());

  /* read alignment dimensions */
  size_t taxa_count, site_count;

  try
  {
    fs >> taxa_count >> site_count;
  }
  catch(exception& e)
  {
    LOG_DEBUG << e.what() << endl;
    throw runtime_error("Invalid alignment dimensions!");
  }

  LOG_DEBUG << "CATG: taxa: " << taxa_count << ", sites: " << site_count << endl;

  string dummy(site_count, '*');

  /* read taxa names */
  try
  {
    string taxon_name;
    for (size_t i = 0; i < taxa_count; ++i)
    {
      fs >> taxon_name;
      msa.append(dummy, taxon_name);
      LOG_DEBUG << "CATG: taxon " << i << ": " << taxon_name << endl;
    }
  }
  catch (exception& e)
  {
    LOG_DEBUG << e.what() << endl;
    throw runtime_error("Wrong number of taxon labels!");
  }

  assert (msa.size() == taxa_count);

  /* this is mapping for DNA: CATG -> ACGT, for other datatypes we assume 1:1 mapping */
  std::vector<size_t> state_map({1, 0, 3, 2});

  string cons_str, prob_str;

  /* read alignment, remember that the matrix is transposed! */
  for (size_t i = 0; i < msa.num_sites(); ++i)
  {
    /* read consensus sequences */
    fs >> cons_str;

    LOG_DEBUG << "CATG: site " << i << " consesus seq: " << cons_str << endl;

    if (cons_str.length() != msa.size())
      throw runtime_error("Wrong length of consensus sequence for site " + to_string(i+1) + "!");

    std::vector<double> probs;

    for (size_t j = 0; j < msa.size(); ++j)
    {
      msa[j][i] = cons_str[j];

      fs >> prob_str;

      if (msa.states() == 0)
      {
        auto states = std::count_if(prob_str.cbegin(), prob_str.cend(),
                                    [](char c) -> bool { return c == ','; }) + 1;
        msa.states(states);

        /* see above: for datatypes other than DNA we assume 1:1 mapping */
        if (states != 4)
          state_map.clear();

        LOG_DEBUG << "CATG: number of states: " << states << endl;
      }

      auto site_probs = msa.probs(j, i);

      istringstream ss(prob_str);
      size_t k = 0;
      for (string token; getline(ss, token, ','); ++k)
      {
        if (state_map.empty())
          site_probs[k] = stod(token);
        else
          site_probs[state_map[k]] = stod(token);
      }

      if (k != msa.states())
        throw runtime_error("Wrong number of state probabilities for site " + to_string(i+1) + "!");
    }
  }

#ifdef CATG_DEBUG
  {
    PhylipStream ps("catgout.phy");
    ps << msa;
  }
#endif

  return stream;
}

MSA msa_load_from_file(const std::string &filename, const FileFormat format)
{
  MSA msa;

  typedef pair<FileFormat, string> FormatNamePair;
  static vector<FormatNamePair> msa_formats = { {FileFormat::phylip, "PHYLIP"},
                                                 {FileFormat::fasta, "FASTA"},
                                                 {FileFormat::catg, "CATG"},
                                                 {FileFormat::vcf, "VCF"},
                                                 {FileFormat::binary, "RAxML-binary"} };

  auto fmt_begin = msa_formats.cbegin();
  auto fmt_end = msa_formats.cend();

  if (!sysutil_file_exists(filename))
    throw runtime_error("File not found: " + filename);

  if (format != FileFormat::autodetect)
  {
    fmt_begin = std::find_if(msa_formats.begin(), msa_formats.end(),
                             [format](const FormatNamePair& p) { return p.first == format; });
    assert(fmt_begin != msa_formats.cend());
    fmt_end = fmt_begin + 1;
  }

  for (; fmt_begin != fmt_end; fmt_begin++)
  {
    try
    {
      switch (fmt_begin->first)
      {
        case FileFormat::fasta:
        {
          FastaStream s(filename);
          s >> msa;
          return msa;
          break;
        }
        case FileFormat::phylip:
        {
          PhylipStream s(filename);
          s >> msa;
          return msa;
          break;
        }
        case FileFormat::catg:
        {
          CATGStream s(filename);
          s >> msa;
          return msa;
          break;
        }
        default:
          throw runtime_error("Unsupported MSA file format!");
      }
    }
    catch(exception &e)
    {
      if (format == FileFormat::autodetect)
        LOG_DEBUG << "Failed to load as " << fmt_begin->second << ": " << e.what() << endl;
      else
        throw runtime_error("Error loading MSA: " + string(e.what()));
    }
  }

  throw runtime_error("Error loading MSA: cannot parse any format supported by RAxML-NG!");
}



