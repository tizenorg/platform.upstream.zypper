#include <iostream>
#include <fstream>
#include <boost/format.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/lexical_cast.hpp>

#include <zypp/target/store/PersistentStorage.h>
#include <zypp/base/IOStream.h>

#include <zypp/RepoManager.h>
#include <zypp/RepoInfo.h>
#include <zypp/repo/RepoException.h>
#include <zypp/parser/ParseException.h>
#include <zypp/media/MediaException.h>

#include "zypper.h"
#include "zypper-tabulator.h"
#include "zypper-callbacks.h"
//#include "AliveCursor.h"
#include "zypper-sources.h"

using namespace std;
using namespace zypp;
using namespace zypp::repo;
using namespace boost;
using namespace zypp::media;
using namespace zypp::parser;

extern ZYpp::Ptr God;
extern RuntimeData gData;
extern Settings gSettings;


static bool refresh_raw_metadata(const RepoInfo & repo, bool force_download)
{
  try
  {
    RepoManager manager;
    manager.refreshMetadata(repo, force_download ?
      RepoManager::RefreshForced : RepoManager::RefreshIfNeeded);
  }
  catch (const RepoNoUrlException & e)
  {
    cerr << format(_("No URLs defined for '%s'.")) % repo.alias() << endl;
    if (!repo.filepath().empty())
      cerr << format(
          _("Please, add one or more base URL (baseurl=URL) entries to %s for repository '%s'."))
          % repo.filepath() % repo.alias() << endl;
    ERR << repo.alias() << " is invalid, disabling it" << endl;

    return true; // error
  }
  catch (const RepoException & e)
  {
    report_problem(e,
        boost::str(format(_("Repository '%s' is invalid.")) % repo.alias()));
    ERR << repo.alias() << " is invalid, disabling it" << endl;

    return true; // error
  }
  catch (const Exception &e)
  {
    ZYPP_CAUGHT(e);
    report_problem(e,
        boost::str(format(_("Error downloading metadata for '%s':")) % repo.alias()));
    // log untranslated message
    ERR << format("Error reading repository '%s':") % repo.alias() << endl;

    return true; // error
  }

  return false; // no error 
}

// ---------------------------------------------------------------------------
/*
bool build_cache_callback(const ProgressData & pd)
{
  static AliveCursor cursor;
  if ( pd.val() == 100 )
    cout << CLEARLN << cursor.done() << " " << pd.name();
  else
    cout << CLEARLN << cursor++ << " " << pd.name();
  cout << " [" << pd.val() << "%] :O)";
  cout << flush;
  return true;
}
*/
static bool build_cache(const RepoInfo &repo, bool force_build)
{
  try
  {
    RepoManager manager;
    manager.buildCache(repo, force_build ?
      RepoManager::BuildForced : RepoManager::BuildIfNeeded);
  }
  catch (const parser::ParseException & e)
  {
    ZYPP_CAUGHT(e);

    report_problem(e,
        boost::str(format(_("Error parsing metadata for '%s':")) % repo.alias()),
        // TranslatorExplanation Don't translate the URL unless it is translated, too
        _("This may be caused by invalid metadata in the repository,"
          " or by a bug in the metadata parser. In the latter case,"
          " or if in doubt, please, file a bug report by folowing"
          " instructions at http://en.opensuse.org/Zypper#Troubleshooting"));

    // log untranslated message
    ERR << format("Error parsing metadata for '%s':") % repo.alias() << endl;

    return true; // error
  }
  catch (const Exception &e)
  {
    ZYPP_CAUGHT(e);
    report_problem(e,
        _("Error building the cache database:"));
    // log untranslated message
    ERR << "Error writing to cache db" << endl;

    return true; // error
  }
  
  return false; // no error
}

// ---------------------------------------------------------------------------

static int do_init_repos()
{
  RepoManager manager;

  string specific_repo = copts.count("repo") ? copts["repo"].front() : "";
  
  // rug compatibility
  //! \todo support repo #
  if (specific_repo.empty())
    specific_repo = copts.count("catalog") ? copts["catalog"].front() : "";

  if (!specific_repo.empty())
  {
    MIL << "--repo set to '" << specific_repo
        << "'. Going to operate on this repo only." << endl;
    try { gData.repos.push_back(manager.getRepositoryInfo(specific_repo)); }
    catch (const repo::RepoNotFoundException & ex)
    {
      cerr << format(_("Repository '%s' not found.")) % specific_repo << endl;
      ERR << specific_repo << " not found";
      return ZYPPER_EXIT_ERR_INVALID_ARGS;
    }
    catch (const Exception & ex)
    {
      cerr << format(_("Error reading repository description file for '%s'."))
          % specific_repo << endl;
      cerr_v << _("Reason: ") << ex.asUserString() << endl;
      ZYPP_CAUGHT(ex);
      return ZYPPER_EXIT_ERR_ZYPP;
    }
  }
  else
    gData.repos = manager.knownRepositories();


  for (std::list<RepoInfo>::iterator it = gData.repos.begin();
       it !=  gData.repos.end(); ++it)
  {
    RepoInfo repo(*it);
    MIL << "checking if to refresh " << repo.alias() << endl;

    //! \todo honor command line options/commands
    bool do_refresh = repo.enabled() && repo.autorefresh(); 

    if (do_refresh)
    {
      cout_v << format(
          _("Checking whether to refresh metadata for %s.")) % repo.alias()
          << endl;
      MIL << "calling refresh for " << repo.alias() << endl;

      // handle root user differently
      if (geteuid() == 0)
      {
        // limit progress reporting only to verbosity level MEDIUM
        gData.limit_to_verbosity = VERBOSITY_MEDIUM;
        if (refresh_raw_metadata(repo, false) || build_cache(repo, false))
        {
          cerr << format(_("Disabling repository '%s' because of the above error."))
              % repo.alias() << endl;
          ERR << format("Disabling repository '%s' because of the above error.")
              % repo.alias() << endl;

          it->setEnabled(false);
        }
        // restore verbosity limit
        gData.limit_to_verbosity = VERBOSITY_NORMAL;
      }
      // non-root user
      else
      {
        try { manager.refreshMetadata(repo, RepoManager::RefreshIfNeeded); }
        // any exception thrown means zypp attempted to refresh the repo
        // i.e. it is out-of-date. Thus, just display refresh hint for non-root
        // user 
        catch (const Exception & ex)
        {
          cout << format(_(
              "Repository '%s' is out-of-date. You can run 'zypper refresh'"
              " as root to update it.")) % repo.alias()
            << endl;

          string nonroot =
            "We're running as non-root, skipping refresh of " + repo.alias(); 
          MIL << nonroot << endl;
          cout_vv << nonroot << endl;
        }
      }
    }
  }

  return ZYPPER_EXIT_OK;
}

// ----------------------------------------------------------------------------

int init_repos()
{
  static bool done = false;
  //! \todo this has to be done so that it works in zypper shell 
  if (done)
    return ZYPPER_EXIT_OK;

  if ( !gSettings.disable_system_sources )
  {
    return do_init_repos();
  }

  done = true;
}

// ----------------------------------------------------------------------------

static void print_repo_list( const std::list<zypp::RepoInfo> &repos )
{
  Table tbl;
  TableHeader th;
  th << "#";
  if (gSettings.is_rug_compatible) th << _("Status");
  else th << _("Enabled") << _("Refresh");
  th << _("Type") << _("Name") << "URI";
  tbl << th;

  int i = 1;

  for (std::list<RepoInfo>::const_iterator it = repos.begin();
       it !=  repos.end(); ++it)
  {
    RepoInfo repo = *it;
    TableRow tr (gSettings.is_rug_compatible ? 5 : 6);
    tr << str::numstring (i);

    // rug's status (active, pending => active, disabled <= enabled, disabled)
    // this is probably the closest possible compatibility arrangement
    if (gSettings.is_rug_compatible)
    {
      tr << (repo.enabled() ? _("Active") : _("Disabled"));
    }
    // zypper status (enabled, autorefresh)
    else
    {
      tr << (repo.enabled() ? _("Yes") : _("No"));
      tr << (repo.autorefresh() ? _("Yes") : _("No"));
    }

    tr << repo.type().asString();
    tr << repo.alias();
    
    for ( RepoInfo::urls_const_iterator uit = repo.baseUrlsBegin();
          uit != repo.baseUrlsEnd();
          ++uit )
    {
      tr << (*uit).asString();
    }
    
    tbl << tr;
    i++;
  }

  if (tbl.empty())
    cout_n << _("No repositories defined."
        " Use 'zypper addrepo' command to add one or more repositories.")
         << endl;
  else
    cout << tbl;
}

// ----------------------------------------------------------------------------

void print_repos_to(const std::list<zypp::RepoInfo> &repos, ostream & out)
{
  for (std::list<RepoInfo>::const_iterator it = repos.begin();
       it !=  repos.end(); ++it)
  {
    it->dumpRepoOn(out);
    out << endl;
  }
}

// ----------------------------------------------------------------------------

void list_repos()
{
  RepoManager manager;
  list<RepoInfo> repos;

  try
  {
    repos = manager.knownRepositories();
  }
  catch ( const Exception &e )
  {
    ZYPP_CAUGHT(e);
    cerr << _("Error reading repositories:") << endl
         << e.asUserString() << endl;
    exit(ZYPPER_EXIT_ERR_ZYPP);
  }

  // export to file or stdout in repo file format
  if (copts.count("export"))
  {
    string filename_str = copts["export"].front();
    if (filename_str == "-")
    {
      print_repos_to(repos, cout);
    }
    else
    {
      if (filename_str.rfind(".repo") == string::npos)
        filename_str += ".repo";

      Pathname file(filename_str);
      std::ofstream stream(file.c_str());
      if (!stream)
      {
        cerr << format(_("Can't open %s for writing. Maybe you don't have write permissions?"))
            % file.asString() << endl;
        exit(ZYPPER_EXIT_ERR_INVALID_ARGS);
      }
      else
      {
        print_repos_to(repos, stream);
        cout << format(
            _("Repositories have been successfully exported to %s."))
            % (file.absolute() ? file.asString() : file.asString().substr(2))
          << endl;
      }
    }
  }
  // print repo list as table
  else
    print_repo_list(repos);
}

// ----------------------------------------------------------------------------

template <typename Target, typename Source>
void safe_lexical_cast (Source s, Target &tr) {
  try {
    tr = boost::lexical_cast<Target> (s);
  }
  catch (boost::bad_lexical_cast &) {
  }
}

// ----------------------------------------------------------------------------

int refresh_repos(vector<string> & arguments)
{
  RepoManager manager;
  list<RepoInfo> repos;
  try
  {
    repos = manager.knownRepositories();
  }
  catch ( const Exception &e )
  {
    ZYPP_CAUGHT(e);
    report_problem(e,
        _("Error reading repositories:"));
    return ZYPPER_EXIT_ERR_ZYPP;
  }

  unsigned error_count = 0;
  unsigned enabled_repo_count = repos.size();
  unsigned repo_number = 0;
  unsigned argc = arguments.size();
  for (std::list<RepoInfo>::iterator it = repos.begin();
       it !=  repos.end(); ++it)
  {
    RepoInfo repo(*it);
    repo_number++;

    if (argc)
    {
      bool specified_found = false;
      
      // search for the repo alias among arguments
      for (vector<string>::iterator it = arguments.begin();
          it != arguments.end(); ++it)
        if ((*it) == repo.alias())
        {
          specified_found = true;
          arguments.erase(it);
          break;
        }
      
      // search for the repo number among arguments
      if (!specified_found)
        for (vector<string>::iterator it = arguments.begin();
            it != arguments.end(); ++it)
        {
          unsigned tmp = 0;
          safe_lexical_cast (*it, tmp);
          if (tmp == repo_number) 
          {
            specified_found = true;
            arguments.erase(it);
            break;
          }
        }

      if (!specified_found)
      {
        DBG << repo.alias() << "(#" << repo_number << ") not specified,"
            << " skipping." << endl;
        enabled_repo_count--;
        continue;
      }
    }

    // skip disabled repos
    if (!repo.enabled())
    {
      string msg = boost::str(
        format(_("Skipping disabled repository '%s'")) % repo.alias());

      if (argc)
        cerr << msg << endl;
      else
        cout_v << msg << endl;

      enabled_repo_count--;
      continue;
    }

    cout_n << format(_("Refreshing '%s'")) % it->alias() << endl;

    // do the refresh
    bool error = false;
    if (!copts.count("build-only"))
    {
      bool force_download =
        copts.count("force") || copts.count("force-download");

      if (force_download)
        cout_v << _("Forcing raw metadata refresh");

      MIL << "calling refreshMetadata" << (force_download ? ", forced" : "")
          << endl;

      error = refresh_raw_metadata(repo, force_download);
    }

    if (!(error || copts.count("download-only")))
    {
      bool force_build = 
        copts.count("force") || copts.count("force-build");

      cout_v << _("Creating repository cache");
      if (force_build)
        cout_v << " " << _("(forced)");
      cout_v << endl;

      MIL << "calling buildCache" << (force_build ? ", forced" : "") << endl;

      error = build_cache(repo, force_build);
    }

    if (error)
    {
      cerr << format(_("Skipping repository '%s' because of the above error."))
          % repo.alias() << endl;
      ERR << format("Skipping repository '%s' because of the above error.")
          % repo.alias() << endl;
      error_count++;
    }
  }

  // the rest of arguments are those not found, complain to the user
  bool show_hint = arguments.size();
  for (vector<string>::iterator it = arguments.begin();
      it != arguments.end();)
  {
    cerr << format(_("Repository '%s' not found by its alias or number.")) % *it
      << endl;
    it = arguments.erase(it);
  }
  if (show_hint)
    cout_n << _("Use 'zypper repos' to get the list of defined repositories.")
      << endl;

  // print the result message
  if (enabled_repo_count == 0)
  {
    if (argc)
      cerr << _("Specified repositories are not enabled or defined.");
    else
      cerr << _("There are no enabled repositories defined.");

    cout_n << endl
      << _("Use 'zypper addrepo' or 'zypper modifyrepo' commands to add or enable repositories.")
      << endl;
  }
  else if (error_count == enabled_repo_count)
    cerr << _("Could not refresh the repositories because of errors.") << endl;
  else if (error_count)
    cerr << _("Some of the repositories have not been refreshed because of error.") << endl;
  else if (argc)
    cout << _("Specified repositories have been refreshed.") << endl;
  else
    cout << _("All repositories have been refreshed.") << endl;
}

// ----------------------------------------------------------------------------

static
std::string timestamp ()
{
  time_t t = time(NULL);
  struct tm * tmp = localtime(&t);

  if (tmp == NULL) {
    return "";
  }

  char outstr[50];
  if (strftime(outstr, sizeof(outstr), "%Y%m%d-%H%M%S", tmp) == 0) {
    return "";
  }
  return outstr;
}

// ----------------------------------------------------------------------------

//! \todo handle zypp exceptions
static
int add_repo(const RepoInfo & repo)
{
  RepoManager manager;

  //cout_v << format(_("Adding repository '%s'.")) % repo.alias() << endl;
  MIL << "Going to add repository: " << repo << endl;

  try
  {
    manager.addRepository(repo);
  }
  catch (const RepoAlreadyExistsException & e)
  {
    ZYPP_CAUGHT(e);
    cerr << format(_("Repository named '%s' already exists. Please, use another alias."))
        % repo.alias() << endl;
    ERR << "Repository named '%s' already exists." << endl;
    return ZYPPER_EXIT_ERR_ZYPP;
  }
  catch (const RepoUnknownTypeException & e)
  {
    ZYPP_CAUGHT(e);
    cerr << _("Can't find a valid repository at given location:") << endl;
    cerr << _("Could not determine the type of the repository."
        " Please, check if the defined URLs (see below) point to a valid repository:");
    for(RepoInfo::urls_const_iterator uit = repo.baseUrlsBegin();
        uit != repo.baseUrlsEnd(); ++uit)
      cerr << (*uit) << endl;
    return ZYPPER_EXIT_ERR_ZYPP;
  }
  catch (const RepoException & e)
  {
    ZYPP_CAUGHT(e);
    report_problem(e,
        _("Problem transfering repository data from specified URL:"),
        _("Please, check whether the specified URL is accessible."));
    ERR << "Problem transfering repository data from specified URL" << endl;
    return ZYPPER_EXIT_ERR_ZYPP;
  }
  catch (const Exception & e)
  {
    ZYPP_CAUGHT(e);
    report_problem(e, _("Unknown problem when adding repository:"));
    return ZYPPER_EXIT_ERR_BUG;
  }

  //! \todo different output for -r and for zypper.
  cout_n << format(_("Repository '%s' successfully added:")) % repo.alias() << endl;
  cout_n << ( repo.enabled() ? "[x]" : "[ ]" );
  cout_n << ( repo.autorefresh() ? "* " : "  " );
  cout_n << repo.alias() << " (" << *repo.baseUrlsBegin() << ")" << endl;

  MIL << "Repository successfully added: " << repo << endl;

  return ZYPPER_EXIT_OK;
}

// ----------------------------------------------------------------------------

int add_repo_by_url( const zypp::Url & url, const string & alias,
                     const string & type,
                     tribool enabled, tribool autorefresh)
{
  RepoManager manager;
  RepoInfo repo;
  
  if ( ! type.empty() )
    repo.setType(RepoType(type));
  
  repo.setAlias(alias.empty() ? timestamp() : alias);
  repo.addBaseUrl(url);
  
  if ( !indeterminate(enabled) )
    repo.setEnabled((enabled == true));
  if ( !indeterminate(autorefresh) )
    repo.setAutorefresh((autorefresh == true));
    
  return add_repo(repo);
}

// ----------------------------------------------------------------------------

//! \todo handle zypp exceptions
int add_repo_from_file(const std::string & repo_file_url,
                       tribool enabled, tribool autorefresh)
{
  //! \todo handle local .repo files, validate the URL
  Url url(repo_file_url);
  RepoManager manager;
  list<RepoInfo> repos = readRepoFile(url);

  for (list<RepoInfo>::const_iterator it = repos.begin();
       it !=  repos.end(); ++it)
  {
    RepoInfo repo = *it;

    MIL << "enabled: " << enabled << " autorefresh: " << autorefresh << endl;
    if ( !indeterminate(enabled) )
      repo.setEnabled((enabled == true));
    if ( !indeterminate(autorefresh) )
      repo.setAutorefresh((autorefresh == true));
    MIL << "enabled: " << repo.enabled() << " autorefresh: " << repo.autorefresh() << endl;
    add_repo(repo);
  }

  return ZYPPER_EXIT_OK;
}

// ----------------------------------------------------------------------------

template<typename T>
ostream& operator << (ostream& s, const vector<T>& v) {
  std::copy (v.begin(), v.end(), ostream_iterator<T> (s, ", "));
  return s;
}

// ----------------------------------------------------------------------------

static
bool looks_like_url (const string& s) {
  static bool schemes_shown = false;
  if (!schemes_shown) {
    cerr_vv << "Registered schemes: " << Url::getRegisteredSchemes () << endl;
    schemes_shown = true;
  }

  string::size_type pos = s.find (':');
  if (pos != string::npos) {
    string scheme (s, 0, pos);
    if (Url::isRegisteredScheme (scheme)) {
      return true;
    }
  }
  return false;
}

static bool do_remove_repo(const RepoInfo & repoinfo)
{
  RepoManager manager;
  bool found = true;
  try
  {
    manager.removeRepository(repoinfo);
    cout << format(_("Repository %s has been removed.")) % repoinfo.alias() << endl;
    MIL << format("Repository %s has been removed.") % repoinfo.alias() << endl;
  }
  catch (const repo::RepoNotFoundException & ex)
  {
    found = false;
  }

  return found;
}


// ----------------------------------------------------------------------------

bool remove_repo( const std::string &alias )
{
  RepoManager manager;
  RepoInfo info;
  info.setAlias(alias);

  return do_remove_repo(info);
}

bool remove_repo(const Url & url, const url::ViewOption & urlview)
{
  RepoManager manager;
  bool found = true;
  try
  {
    RepoInfo info = manager.getRepositoryInfo(url, urlview);
    found = do_remove_repo(info);
  }
  catch (const repo::RepoNotFoundException & ex)
  {
    found = false;
  }

  return found;
}

// ----------------------------------------------------------------------------

void rename_repo(const std::string & alias, const std::string & newalias)
{
  RepoManager manager;

  try
  {
    RepoInfo repo(manager.getRepositoryInfo(alias));
    repo.setAlias(newalias);
    manager.modifyRepository(alias, repo);

    cout << format(_("Repository %s renamed to %s")) % alias % repo.alias() << endl;
    MIL << format("Repository %s renamed to %s") % alias % repo.alias() << endl;
  }
  catch (const RepoNotFoundException & ex)
  {
    cerr << format(_("Repository %s not found.")) % alias << endl;
    ERR << "Repo " << alias << " not found" << endl; 
  }
  catch (const Exception & ex)
  {
    cerr << _("Error while modifying the repository:") << endl;
    cerr << ex.asUserString() << endl;
    cerr << format(_("Leaving repository %s unchanged.")) % alias << endl;

    ERR << "Error while modifying the repository:" << ex.asUserString() << endl;
  }
}

// ----------------------------------------------------------------------------

void modify_repo(const string & alias)
{
  // tell whether currenlty processed options are contradicting each other
  bool contradiction = false;
  // TranslatorExplanation speaking of two mutually contradicting command line options
  string msg_contradition =
    _("%s used together with %s, which contradict each other."
      " This property will be left unchanged.");

  // enable/disable repo
  tribool enable = indeterminate;
  if (copts.count("enable"))
    enable = true;
  if (copts.count("disable"))
  {
    if (enable)
    {
      cerr << format(msg_contradition) % "--enable" % "--disable" << endl;
      
      enable = indeterminate;
    }
    else
      enable = false;
  }
  DBG << "enable = " << enable << endl;

  // autorefresh
  tribool autoref = indeterminate;
  if (copts.count("enable-autorefresh"))
    autoref = true;
  if (copts.count("disable-autorefresh"))
  {
    if (autoref)
    {
      cerr << format(msg_contradition)
        % "--enable-autorefresh" % "--disable-autorefresh" << endl;

      autoref = indeterminate;
    }
    else
      autoref = false;
  }
  DBG << "autoref = " << autoref << endl;

  try
  {
    RepoManager manager;
    RepoInfo repo(manager.getRepositoryInfo(alias));

    if (!indeterminate(enable))
      repo.setEnabled(enable);

    if (!indeterminate(autoref))
      repo.setAutorefresh(autoref);

    manager.modifyRepository(alias, repo);

    cout << format(_("Repository %s has been sucessfully modified.")) % alias << endl;
    MIL << format("Repository %s modified:") % alias << repo << endl;
  }
  catch (const RepoNotFoundException & ex)
  {
    cerr << format(_("Repository %s not found.")) % alias << endl;
    ERR << "Repo " << alias << " not found" << endl; 
  }
  catch (const Exception & ex)
  {
    cerr << _("Error while modifying the repository:") << endl;
    cerr << ex.asUserString();
    cerr << format(_("Leaving repository %s unchanged.")) % alias << endl;

    ERR << "Error while modifying the repository:" << ex.asUserString() << endl;
  }
}

/*
//! rename a source, identified in any way: alias, url, id
void rename_source( const std::string& anystring, const std::string& newalias )
{
  cerr_vv << "Constructing SourceManager" << endl;
  SourceManager_Ptr manager = SourceManager::sourceManager();
  cerr_vv << "Restoring SourceManager" << endl;
  manager->restore (gSettings.root_dir, true /*use_cache*//*);

  Source_Ref src;

  SourceManager::SourceId sid = 0;
  safe_lexical_cast (anystring, sid);
  if (sid > 0) {
    try {
      src = manager->findSource (sid);
    }
    catch (const Exception & ex) {
      ZYPP_CAUGHT (ex);
      // boost::format: %s is fine regardless of the actual type :-)
      cerr << format (_("Source %s not found.")) % sid << endl;
    }
  }
  else {
    bool is_url = false;
    if (looks_like_url (anystring)) {
	is_url = true;
	cerr_vv << "Looks like a URL" << endl;

	Url url;
	try {
	  url = Url (anystring);
	}
	catch ( const Exception & excpt_r ) {
	  ZYPP_CAUGHT( excpt_r );
	  cerr << _("URL is invalid: ") << excpt_r.asUserString() << endl;
	}
	if (url.isValid ()) {
	  try {
	    src = manager->findSourceByUrl (url);
	  }
	  catch (const Exception & ex) {
	    ZYPP_CAUGHT (ex);
	    cerr << format (_("Source %s not found.")) % url.asString() << endl;
	  }
	}
    }

    if (!is_url) {
      try {
	src = manager->findSource (anystring);
      }
      catch (const Exception & ex) {
	ZYPP_CAUGHT (ex);
	cerr << format (_("Source %s not found.")) % anystring << endl;
      }
    }
  }

  if (src) {
    // getting Source_Ref is useless if we only can use an id
    manager->renameSource (src.numericId (), newalias);
  }

  cerr_vv << "Storing source data" << endl;
  manager->store( gSettings.root_dir, true /*metadata_cache*//* );
}
*/
// ----------------------------------------------------------------------------

// #217028
void warn_if_zmd()
{
  if (system ("pgrep -lx zmd") == 0)
  { // list name, exact match
    cout_n << _("ZENworks Management Daemon is running.\n"
              "WARNING: this command will not synchronize changes.\n"
              "Use rug or yast2 for that.\n");
    USR << ("ZMD is running. Tell the user this will get"
        " ZMD and libzypp out of sync.") << endl;
  }
}

// ----------------------------------------------------------------------------

// OLD code
/*
void cond_init_system_sources ()
{
  static bool done = false;
  if (done)
    return;

  if ( geteuid() != 0 ) {
    cerr << _("Sorry, you need root privileges to use system sources, disabling them...") << endl;
    gSettings.disable_system_sources = true;
    MIL << "system sources disabled" << endl;
  }

  if ( ! gSettings.disable_system_sources ) {
    init_system_sources();
  }
  done = true;
} 
*/
// OLD
/*
void init_system_sources()
{
  SourceManager_Ptr manager;
  manager = SourceManager::sourceManager();
  try
  {
    cerr << _("Restoring system sources...") << endl;
    manager->restore(gSettings.root_dir);
  }
//  catch (const SourcesAlreadyRestoredException& excpt) {
//  }
  catch (Exception & excpt_r)
  {
    ZYPP_CAUGHT (excpt_r);
    ERR << "Couldn't restore sources" << endl;
    cerr << _("Failed to restore sources") << endl;
    exit(-1);
  }
    
  for ( SourceManager::Source_const_iterator it = manager->Source_begin(); it !=  manager->Source_end(); ++it )
  {
    Source_Ref src = manager->findSource(it->alias());
    gData.sources.push_back(src);
  }
}
*/
// OLD
/*
void include_source_by_url( const Url &url )
{
  try
  {
    //cout << "Creating source from " << url << endl;
    Source_Ref src;
    src = SourceFactory().createFrom(url, "/", url.asString(), "");
    //cout << "Source created.. " << endl << src << endl;
    gData.sources.push_back(src);
  }
  catch( const Exception & excpt_r )
  {
    cerr << _("Can't access repository") << endl;
    ZYPP_CAUGHT( excpt_r );
    exit(-1);
  }

}
*/

// Local Variables:
// c-basic-offset: 2
// End:
