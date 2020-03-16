// Copyright © 2010-20 sss
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "stdafx.h"

using namespace boost::process::initializers;
using namespace boost::iostreams;

pxResult gpg_execution_params::pxExecute()
{
	if (!globals.gpg_valid)
		return pxNotConfigured;

	CMStringW bin_path(g_plugin.getMStringW("szGpgBinPath"));
	if (!boost::filesystem::exists(bin_path.c_str())) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": GPG executable not found");
		return result = pxNotFound;
	}

	std::vector<std::wstring> argv;
	std::vector<std::wstring> env;
	env.push_back(L"LANGUAGE=en@quot");
	env.push_back(L"LC_ALL=English");
	env.push_back(L"LANG=C");
	argv.push_back(bin_path.c_str());

	CMStringW home_dir(g_plugin.getMStringW("szHomePath"));
	if (!home_dir.IsEmpty())  { // this check are required for first run gpg binary validation
		argv.push_back(L"--homedir");
		argv.push_back(home_dir.c_str());
	}

	argv.push_back(L"--display-charset");
	argv.push_back(L"utf-8");
	argv.push_back(L"-z9");
	argv.insert(argv.end(), aargv.begin(), aargv.end());

	if (globals.bDebugLog) {
		std::wstring args;
		for (unsigned int i = 0; i < argv.size(); ++i) {
			args += argv[i];
			args += L" ";
		}
		args.erase(args.size() - 1, 1);
		globals.debuglog << std::string(time_str() + ": gpg in: " + toUTF8(args));
	}

	out.Empty();
	boost::process::pipe pout = boost::process::create_pipe();
	boost::process::pipe perr = boost::process::create_pipe();
	boost::process::child *c = nullptr;
	{
		file_descriptor_sink sout(pout.sink, close_handle);
		file_descriptor_sink serr(perr.sink, close_handle);

		wchar_t *mir_path = new wchar_t[MAX_PATH];
		PathToAbsoluteW(L"\\", mir_path);

		c = new boost::process::child(boost::process::execute(set_args(argv), bind_stdout(sout), bind_stderr(serr), close_stdin(), show_window(SW_HIDE), hide_console(), inherit_env(), set_env(env), start_in_dir(mir_path)));
		this->child = c;

		delete[] mir_path;
	}

	file_descriptor_source source(pout.source, close_handle);

	stream<file_descriptor_source> is(source);

	try {
		std::string s;
		while (std::getline(is, s)) {
			out.Append(s.c_str());
			out.Append("\n");
		}
	}
	catch (const std::exception &e) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": failed to read from stream with error: " + e.what() + "\n\tSuccesfully read : " + out.c_str());
	}

	file_descriptor_source source2(perr.source, close_handle);

	stream<file_descriptor_source> is2(source2);

	try {
		std::string s;
		while (std::getline(is2, s)) {
			out.Append(s.c_str());
			out.Append("\n");
		}
	}
	catch (const std::exception &e) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": failed to read from stream with error: " + e.what() + "\n\tSuccesfully read : " + out.c_str());
	}

	out.Replace("\r\r", "");

	if (globals.bDebugLog)
		globals.debuglog << std::string(time_str() + ": gpg out: " + out.c_str());

	auto ec = wait_for_exit(*c);
	delete c;
	this->code = ec;
	this->child = nullptr;

	if (ec) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": warning: wrong gpg exit status, gpg output: " + out.c_str());
		return pxSuccessExitCodeInvalid;
	}

	return pxSuccess;
}

void pxEexcute_thread(gpg_execution_params *params)
{
	params->pxExecute();
}

bool gpg_launcher(gpg_execution_params &params, boost::posix_time::time_duration t)
{
	bool ret = true;
	HANDLE hThread = mir_forkThread<gpg_execution_params>(pxEexcute_thread, &params);
	if (WaitForSingleObject(hThread, t.total_milliseconds()) == WAIT_TIMEOUT) {
		ret = false;
		if (params.child)
			boost::process::terminate(*(params.child));
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": GPG execution timed out, aborted");
	}
	return ret;
}

pxResult pxExecute_passwd_change(std::vector<std::wstring> &aargv, pxResult *result, boost::process::child *_child)
{
	if (!globals.gpg_valid)
		return pxNotConfigured;

	CMStringW bin_path(g_plugin.getMStringW("szGpgBinPath"));
	if (!boost::filesystem::exists(bin_path.c_str())) {
		if (globals.bDebugLog)
			globals.debuglog << std::string(time_str() + ": GPG executable not found");
		*result = pxNotFound;
		return pxNotFound;
	}

	using namespace boost::process;
	using namespace boost::process::initializers;
	using namespace boost::iostreams;

	std::vector<std::wstring> argv;
	std::vector<std::wstring> env;
	env.push_back(L"LANGUAGE=en@quot");
	env.push_back(L"LC_ALL=English");
	argv.push_back(bin_path.c_str());
	argv.push_back(L"--homedir");
	argv.push_back(g_plugin.getMStringW("szHomePath").c_str());
	argv.push_back(L"--display-charset");
	argv.push_back(L"utf-8");
	argv.push_back(L"-z9");
	argv.insert(argv.end(), aargv.begin(), aargv.end());

//	pipe pout = create_pipe();
//	pipe pin = create_pipe();
//	file_descriptor_sink sout(pout.sink, close_handle);
//	file_descriptor_source sin(pin.source, close_handle);

	char *mir_path = new char [MAX_PATH];
	PathToAbsolute("\\", mir_path);

	//execute(set_args(argv), bind_stdout(sout), bind_stdin(sin), show_window(SW_HIDE), hide_console(), inherit_env(), set_env(env), start_in_dir(toUTF16(mir_path)));
	child c = execute(set_args(argv), /*bind_stdin(sin), */inherit_env(), set_env(env), start_in_dir(toUTF16(mir_path)));
	_child = &c;

	delete [] mir_path;
	
//	file_descriptor_source source(pout.source, close_handle);
/*	file_descriptor_sink _sin(pin.sink, close_handle);

	stream<file_descriptor_sink> out(_sin);


	std::wstring cmd;
	for(int i = 0; i < argv.size(); i++)
	{
		cmd += argv[i];
		cmd += L" ";
	}
	*/
//	out<<toUTF8(cmd)<<std::endl;

	//fucked gpg does not want to give us stdin/stdout
	wait_for_exit(*_child);
	_child = nullptr;

/*	out<<old_pass<<std::endl;
	out<<new_pass<<std::endl;
	out<<new_pass<<std::endl;
	out<<"save"<<std::endl; */

//	stream<file_descriptor_source> is(source);
/*	std::string s;

	is>>s;
	
	MessageBoxA(NULL, s.c_str(), "", MB_OK); */


/*	ctx.environment = boost::process::self::get_environment();
	ctx.environment.insert(boost::process::environment::value_type("LANGUAGE", "en@quot"));
	ctx.environment["LC_ALL"] = "English";

	char *mir_path = new char [MAX_PATH];
	PathToAbsolute("\\", mir_path);
	ctx.work_directory = mir_path;
	delete [] mir_path;

	boost::process::win32_child proc = boost::process::win32_launch(toUTF8(bin_path), argv, ctx);

	mir_free(bin_path);

//	boost::process::pistream &_stdout = proc.get_stdout();
	boost::process::postream &_stdin = proc.get_stdin();

	::Sleep(50);

	std::string buf; */


/*	std::getline(_stdout, buf);
	while(_stdout.good())
	{
		*aoutput += buf;
		if(out.find("Enter passphrase") != std::string::npos)
			break;
		::Sleep(50);
		std::getline(_stdout, buf);
	}
	*aoutput += buf; */

	//_stdin<<old_pass<<std::endl;

/*	std::getline(_stdout, buf);
	while(_stdout.good())
	{
		*aoutput += buf;
		if(out.find("Enter the new passphrase for this secret key.") != std::string::npos)
			break;
		::Sleep(50);
		std::getline(_stdout, buf);
	}

	*aoutput += buf;

	if(out.find("Enter passphrase") != std::string::npos)*/
		//_stdin<<new_pass<<std::endl;

/*	std::getline(_stdout, buf);
	while(_stdout.good())
	{
		*aoutput += buf;
		if(out.find("Repeat passphrase") != std::string::npos)
			break;
		::Sleep(50);
		std::getline(_stdout, buf);
	}
	*aoutput += buf; */

	//_stdin<<new_pass<<std::endl;

/*	std::getline(_stdout, buf);
	while(_stdout.good())
	{
		*aoutput += buf;
		if(out.find("Command") != std::string::npos)
			break;
		::Sleep(50);
		std::getline(_stdout, buf);
	}
	*aoutput += buf; */

	//_stdin<<"save"<<std::endl; 

	//proc.wait();

	//MessageBoxA(NULL, out.c_str(), "info", MB_OK);

	return pxSuccess;
}

void pxEexcute_passwd_change_thread(void *param)
{
	gpg_execution_params_pass *params = (gpg_execution_params_pass*)param;
	pxExecute_passwd_change(params->aargv, &params->result, params->child);
}
