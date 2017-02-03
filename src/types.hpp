#ifndef RAXML_TYPES_HPP_
#define RAXML_TYPES_HPP_

enum class StartingTree
{
  random,
  parsimony,
  user
};

enum class Command
{
  none = 0,
  help,
  version,
  evaluate,
  search
};

enum class FileFormat
{
  autodetect = 0,
  fasta,
  phylip,
  vcf,
  catg,
  binary
};

enum class DataType
{
  autodetect = 0,
  dna,
  protein,
  binary,
  multistate,
  diploid10
};

enum class ParamValue
{
  undefined = 0,
  equal,
  user,
  model,
  empirical,
  ML
};

/*
 * workaround needed for using enum as std::map key
 * code from: http://stackoverflow.com/a/24847480
 * */
struct EnumClassHash
{
  template <typename T>
  std::size_t operator()(T t) const
  {
      return static_cast<std::size_t>(t);
  }
};

/* generic exception class */
class RaxmlException : public std::exception
{
public:
  RaxmlException(const std::string& message)
  : _message(message)
  {
  }

  virtual const char* what() const noexcept { return message().c_str(); }

  virtual const std::string message() const { return _message; };

protected:
  std::string _message;

  template<typename ... Args>
  std::string format_message(const std::string& fmt, Args ... args) const
  {
    size_t size = std::snprintf(nullptr, 0, fmt.c_str(), args ...) + 1;
    std::string msg;
    msg.reserve(size);
    std::snprintf(&msg[0], size, fmt.c_str(), args ...);
    return msg;
  }
};


#endif /* RAXML_TYPES_HPP_ */
