@rem Run Mineways in a way to avoid loading any worlds stored on your machine,
@rem and generate a debug logfile of what's going on. This can help you figure out where things
@rem are going wrong: run Mineways and then look at mineways_exec.log.
@rem If you're still stumped, contact me, Eric Haines, at http://mineways.com/contact.html
@rem This script gives a directory that exists but (probably) is not where your worlds are stored,
@rem so that none are loaded.
mineways.exe -l mineways_exec.log -s none