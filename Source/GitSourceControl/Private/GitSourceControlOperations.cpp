// Copyright (c) 2014-2015 Sebastien Rombauts (sebastien.rombauts@gmail.com)
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "GitSourceControlPrivatePCH.h"
#include "GitSourceControlOperations.h"
#include "GitSourceControlState.h"
#include "GitSourceControlCommand.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlUtils.h"

#define LOCTEXT_NAMESPACE "GitSourceControl"

FName FGitConnectWorker::GetName() const
{
	return "Connect";
}

bool FGitConnectWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = GitSourceControlUtils::FindRootDirectory(InCommand.PathToGameDir, InCommand.PathToRepositoryRoot);
	if(InCommand.bCommandSuccessful)
	{
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("status"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);
	}
	if(!InCommand.bCommandSuccessful || InCommand.ErrorMessages.Num() > 0 || InCommand.InfoMessages.Num() == 0)
	{
		// @todo popup to propose to initialize the git repository "git init + .gitignore"
		StaticCastSharedRef<FConnect>(InCommand.Operation)->SetErrorText(LOCTEXT("NotAWorkingCopyError", "Project is not part of a Git working copy."));
		// @todo Double error messages (and displayed in reverse order): Perforce distinguish the two errors
		InCommand.ErrorMessages.Add(LOCTEXT("NotAWorkingCopyErrorHelp", "You should check out a working copy into your project directory.").ToString());
		InCommand.bCommandSuccessful = false;
	}
	else // if(InCommand.bCommandSuccessful)
	{
		TArray<FString> Parameters;
		Parameters.Add("--short HEAD");

		// Get current branche name
		GitSourceControlUtils::RunCommand(TEXT("symbolic-ref"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);
		if(InCommand.InfoMessages.Num() == 1)
		{
			InCommand.BranchName = InCommand.InfoMessages[0];
		}
	}

	return InCommand.bCommandSuccessful;
}

bool FGitConnectWorker::UpdateStates() const
{
	return false;
}

static FText ParseCommitResults(const TArray<FString>& InResults)
{
	if(InResults.Num() >= 1)
	{
		const FString& FirstLine = InResults[0];
		return FText::Format(LOCTEXT("CommitMessage", "Commited {0}."), FText::FromString(FirstLine));
	}
	return LOCTEXT("CommitMessageUnknown", "Submitted revision.");
}

FName FGitCheckInWorker::GetName() const
{
	return "CheckIn";
}

bool FGitCheckInWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);

	// make a temp file to place our commit message in
	FScopedTempFile CommitMsgFile(Operation->GetDescription());
	if(CommitMsgFile.GetFilename().Len() > 0)
	{
		TArray<FString> Parameters;
		FString ParamCommitMsgFilename = TEXT("--file=\"");
		ParamCommitMsgFilename += FPaths::ConvertRelativePathToFull(CommitMsgFile.GetFilename());
		ParamCommitMsgFilename += TEXT("\"");
		Parameters.Add(ParamCommitMsgFilename);

		InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommit(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);
		if(InCommand.bCommandSuccessful)
		{
			Operation->SetSuccessMessage(ParseCommitResults(InCommand.InfoMessages));
			UE_LOG(LogSourceControl, Log, TEXT("FGitCheckInWorker: commit successful"));
		}
	}

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitCheckInWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

FName FGitMarkForAddWorker::GetName() const
{
	return "MarkForAdd";
}

bool FGitMarkForAddWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("add"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitMarkForAddWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

FName FGitDeleteWorker::GetName() const
{
	return "Delete";
}

bool FGitDeleteWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("rm"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitDeleteWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

FName FGitRevertWorker::GetName() const
{
	return "Revert";
}

bool FGitRevertWorker::Execute(FGitSourceControlCommand& InCommand)
{
	// reset any changes already added in index
	{
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	// revert any changes in working copy
	{
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("checkout"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitRevertWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

FName FGitUpdateStatusWorker::GetName() const
{
	return "UpdateStatus";
}

bool FGitUpdateStatusWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FUpdateStatus>(InCommand.Operation);

	if(InCommand.Files.Num() > 0)
	{
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Files, InCommand.ErrorMessages, States);

		if(Operation->ShouldUpdateHistory())
		{
			for(const auto& ItFile : InCommand.Files)
			{
				TArray<FString> Results;
				TArray<FString> Parameters;

				Parameters.Add(TEXT("--max-count 100"));
				Parameters.Add(TEXT("--follow")); // follow file renames
				Parameters.Add(TEXT("--date=raw"));
				Parameters.Add(TEXT("--name-status")); // relative filename at this revision, preceded by a status character

				TArray<FString> Files;
				Files.Add(*ItFile);

				InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("log"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, Files, Results, InCommand.ErrorMessages);
				TGitSourceControlHistory History;
				GitSourceControlUtils::ParseLogResults(Results, History);
				Histories.Add(*ItFile, History);
			}
		}
	}
	else
	{
		// Perforce "opened files" are those that have been modified (or added/deleted): that is what we get with a simple Git status from the root
		if(Operation->ShouldGetOpenedOnly())
		{
			TArray<FString> Files;
			Files.Add(FPaths::ConvertRelativePathToFull(FPaths::GameDir()));
			InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Files, InCommand.ErrorMessages, States);
		}
	}

	// don't use the ShouldUpdateModifiedState() hint here as it is specific to Perforce: the above normal Git status has already told us this information (like Git and Mercurial)

	return InCommand.bCommandSuccessful;
}

bool FGitUpdateStatusWorker::UpdateStates() const
{
	bool bUpdated = GitSourceControlUtils::UpdateCachedStates(States);

	FGitSourceControlModule& GitSourceControl = FModuleManager::LoadModuleChecked<FGitSourceControlModule>( "GitSourceControl" );
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

	// add history, if any
	for(const auto& ItHistory : Histories)
	{
		TSharedRef<FGitSourceControlState, ESPMode::ThreadSafe> State = Provider.GetStateInternal(ItHistory.Key);
		State->History = ItHistory.Value;
		State->TimeStamp = FDateTime::Now();
		bUpdated = true;
	}

	return bUpdated;
}

FName FGitCopyWorker::GetName() const
{
	return "Copy";
}

bool FGitCopyWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	// Implement a no-op as Git does not need an explicit copy
	InCommand.bCommandSuccessful = true;

	return InCommand.bCommandSuccessful;
}

bool FGitCopyWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(OutStates);
}

/* @todo
FName FGitResolveWorker::GetName() const
{
	return "Resolve";
}

bool FGitResolveWorker::Execute( class FGitSourceControlCommand& InCommand )
{
	check(InCommand.Operation->GetName() == GetName());

	// mark the conflicting files as resolved:
	{
		TArray<FString> Results;
		TArray<FString> ResolveParameters;
		ResolveParameters.Add(TEXT("--accept mine-full"));
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("resolve"), InCommand.Files, ResolveParameters, Results, InCommand.ErrorMessages, InCommand.UserName);
	}

	// now update the status of our files
	{
		TArray<FXmlFile> ResultsXml;
		TArray<FString> StatusParameters;
		StatusParameters.Add(TEXT("--verbose"));
		StatusParameters.Add(TEXT("--show-updates"));

		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("status"), InCommand.Files, StatusParameters, ResultsXml, InCommand.ErrorMessages, InCommand.UserName);
		GitSourceControlUtils::ParseStatusResults(ResultsXml, InCommand.ErrorMessages, InCommand.UserName, InCommand.WorkingCopyRoot, OutStates);
	}

	return InCommand.bCommandSuccessful;
}

bool FGitResolveWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(OutStates);
}
*/

FName FGitInitWorker::GetName() const
{
	return "Init";
}

bool FGitInitWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());
	
	InCommand.bCommandSuccessful = false;

	if (GitSourceControlUtils::RunCommand(TEXT("init"), InCommand.PathToGitBinary, InCommand.PathToGameDir, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages)){
		InCommand.bCommandSuccessful = true;
	}

	return InCommand.bCommandSuccessful;
}

bool FGitInitWorker::UpdateStates() const
{
	return false;
}

#undef LOCTEXT_NAMESPACE
