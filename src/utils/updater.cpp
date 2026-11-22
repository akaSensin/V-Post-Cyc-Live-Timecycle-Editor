#include "updater.h"

namespace updater
{
	void checkAsync()
	{
	}

	Status status()
	{
		return Status::Idle;
	}

	std::string statusLabel()
	{
		return std::string("v") + kModVersion;
	}

	void openReleasesPage()
	{
	}

	void openGithubPage()
	{
	}
}
