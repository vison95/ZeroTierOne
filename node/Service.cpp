/*
 * ZeroTier One - Global Peer to Peer Ethernet
 * Copyright (C) 2012-2013  ZeroTier Networks LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * ZeroTier may be used and distributed under the terms of the GPLv3, which
 * are available at: http://www.gnu.org/licenses/gpl-3.0.html
 *
 * If you would like to embed ZeroTier into a commercial application or
 * redistribute it in a modified binary form, please contact ZeroTier Networks
 * LLC. Start here: http://www.zerotier.com/
 */

#include "Constants.hpp"

#ifndef __WINDOWS__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>

#include "Service.hpp"
#include "RuntimeEnvironment.hpp"
#include "Utils.hpp"
#include "Logger.hpp"

namespace ZeroTier {

Service::Service(const RuntimeEnvironment *renv,const char *name,const char *path,void (*handler)(void *,Service &,const Dictionary &),void *arg) :
	_r(renv),
	_path(path),
	_name(name),
	_arg(arg),
	_handler(handler),
	_pid(-1),
	_childStdin(0),
	_childStdout(0),
	_childStderr(0),
	_run(true)
{
	start();
}

Service::~Service()
{
	_run = false;
	long pid = _pid;
	if (pid > 0) {
		int st = 0;
		::kill(pid,SIGTERM);
		for(int i=0;i<20;++i) {
			if (waitpid(pid,&st,WNOHANG) == pid) {
				pid = 0;
				break;
			}
			Thread::sleep(100);
		}
		if (pid > 0) {
			::kill(pid,SIGKILL);
			waitpid(pid,&st,0);
		}
	}
	join();
}

bool Service::send(const Dictionary &msg)
{
	if (_childStdin <= 0)
		return false;

	std::string mser = msg.toString();
	if (mser.length() > ZT_SERVICE_MAX_MESSAGE_SIZE)
		return false;

	// This can technically block. We'll fix this if it ends up being a
	// problem.
	uint32_t len = Utils::hton((uint32_t)mser.length());
	if (write(_childStdin,&len,4) != 4)
		return false;
	if ((int)write(_childStdin,mser.data(),mser.length()) != (int)mser.length())
		return false;

	return true;
}

void Service::main()
	throw()
{
	fd_set readfds,writefds,exceptfds;
	struct timeval tv;

	while (_run) {
		if (_pid <= 0) {
			LOG("launching service %s...",_name.c_str());

			int in[2],out[2],err[2];
			pipe(in);
			pipe(out);
			pipe(err);

			long pid = fork();
			if (pid < 0) {
				LOG("service %s terminating: could not fork!",_name.c_str());
				return;
			} else if (pid) {
				close(in[1]);
				close(out[0]);
				close(err[0]);
				Thread::sleep(500); // give child time to start
				_childStdin = in[1];
				_childStdout = out[0];
				_childStderr = err[0];
			} else {
				dup2(in[0],STDIN_FILENO);
				dup2(out[1],STDOUT_FILENO);
				dup2(err[1],STDERR_FILENO);
				execl(_path.c_str(),_path.c_str(),_r->homePath.c_str(),(const char *)0);
				exit(-1);
			}
		} else {
			int st = 0;
			if (waitpid(_pid,&st,WNOHANG) == _pid) {
				if (_childStdin > 0) close(_childStdin);
				_childStdin = 0;
				if (_childStdout > 0) close(_childStdout);
				if (_childStderr > 0) close(_childStderr);
				_pid = 0;

				if (!_run)
					return;

				LOG("service %s exited with exit code: %d, delaying 1s to attempt relaunch",_name.c_str(),st);

				Thread::sleep(1000); // wait to relaunch
				continue;
			}
		}

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		FD_SET(_childStdout,&readfds);
		FD_SET(_childStderr,&readfds);

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		select(std::max(_childStdout,_childStderr)+1,&readfds,&writefds,&exceptfds,&tv);

		if (!_run) {
			if (_childStdin > 0) close(_childStdin);
			_childStdin = 0;
			if (_childStdout > 0) close(_childStdout);
			if (_childStderr > 0) close(_childStderr);
			return;
		}

		if ((_childStderr > 0)&&(FD_ISSET(_childStderr,&readfds))) {
		}

		if ((_childStdout > 0)&&(FD_ISSET(_childStdout,&readfds))) {
		}
	}
}

} // namespace ZeroTier

#endif // __WINDOWS__

