/** 
 * @file LLIMMgr.cpp
 * @brief Container for Instant Messaging
 *
 * $LicenseInfo:firstyear=2001&license=viewergpl$
 * 
 * Copyright (c) 2001-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llimview.h"

#include "llfontgl.h"
#include "llrect.h"
#include "llbutton.h"
#include "llhttpclient.h"
#include "llsdutil_math.h"
#include "llstring.h"
#include "lltrans.h"
#include "lluictrlfactory.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llavataractions.h"
#include "llcallingcard.h"
#include "llchat.h"
#include "llresmgr.h"
#include "llfloaterchat.h"
#include "llfloaterchatterbox.h"
#include "llhttpnode.h"
#include "llimpanel.h"
#include "llnotificationsutil.h"
#include "llsdserialize.h"
#include "llspeakers.h"
#include "lltabcontainer.h"
#include "llmutelist.h"
#include "llviewermenu.h"
#include "llviewermessage.h"
#include "llviewerwindow.h"
#include "llvoavatar.h" // For mIdleTimer reset
#include "llnotify.h"
#include "llviewerregion.h"

// [RLVa:KB]
#include "rlvhandler.h"
// [/RLVa:KB]

class AIHTTPTimeoutPolicy;
extern AIHTTPTimeoutPolicy viewerChatterBoxInvitationAcceptResponder_timeout;

//
// Globals
//
LLIMMgr* gIMMgr = NULL;

//
// Helper Functions
//
LLVOAvatar* find_avatar_from_object(const LLUUID& id);

LLColor4 agent_chat_color(const LLUUID& id, const std::string& name, bool local_chat)
{
	if (id.isNull() || (name == SYSTEM_FROM))
		return gSavedSettings.getColor4("SystemChatColor");

	if (id == gAgentID)
		return gSavedSettings.getColor4("UserChatColor");

	static const LLCachedControl<bool> color_linden_chat("ColorLindenChat");
	if (color_linden_chat && LLMuteList::getInstance()->isLinden(name))
		return gSavedSettings.getColor4("AscentLindenColor");

	// [RLVa:LF] Chat colors would identify names, don't use them in local chat if restricted
	if (local_chat && rlv_handler_t::isEnabled() && gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES))
		return gSavedSettings.getColor4("AgentChatColor");

	static const LLCachedControl<bool> color_friend_chat("ColorFriendChat");
	if (color_friend_chat && LLAvatarTracker::instance().isBuddy(id))
		return gSavedSettings.getColor4("AscentFriendColor");

	static const LLCachedControl<bool> color_eo_chat("ColorEstateOwnerChat");
	if (color_eo_chat)
	{
		const LLViewerRegion* parent_estate = gAgent.getRegion();
		if (parent_estate && id == parent_estate->getOwner())
			return gSavedSettings.getColor4("AscentEstateOwnerColor");
	}

	return local_chat ? gSavedSettings.getColor4("AgentChatColor") : gSavedSettings.getColor("IMChatColor");
}

// returns true if a should appear before b
//static BOOL group_dictionary_sort( LLGroupData* a, LLGroupData* b )
//{
//	return (LLStringUtil::compareDict( a->mName, b->mName ) < 0);
//}

class LLViewerChatterBoxInvitationAcceptResponder : public LLHTTPClient::ResponderWithResult
{
public:
	LLViewerChatterBoxInvitationAcceptResponder(
		const LLUUID& session_id,
		LLIMMgr::EInvitationType invitation_type)
	{
		mSessionID = session_id;
		mInvitiationType = invitation_type;
	}

	/*virtual*/ void result(const LLSD& content)
	{
		if ( gIMMgr)
		{
			LLFloaterIMPanel* floater = gIMMgr->findFloaterBySession(mSessionID);
			LLIMSpeakerMgr* speaker_mgr = floater ? floater->getSpeakerManager() : NULL;
			if (speaker_mgr)
			{
				//we've accepted our invitation
				//and received a list of agents that were
				//currently in the session when the reply was sent
				//to us.  Now, it is possible that there were some agents
				//to slip in/out between when that message was sent to us
				//and now.

				//the agent list updates we've received have been
				//accurate from the time we were added to the session
				//but unfortunately, our base that we are receiving here
				//may not be the most up to date.  It was accurate at
				//some point in time though.
				speaker_mgr->setSpeakers(content);

				//we now have our base of users in the session
				//that was accurate at some point, but maybe not now
				//so now we apply all of the udpates we've received
				//in case of race conditions
				speaker_mgr->updateSpeakers(gIMMgr->getPendingAgentListUpdates(mSessionID));
			}

			if (LLIMMgr::INVITATION_TYPE_VOICE == mInvitiationType)
			{
				gIMMgr->startCall(mSessionID, LLVoiceChannel::INCOMING_CALL);
			}

			if ((mInvitiationType == LLIMMgr::INVITATION_TYPE_VOICE
				|| mInvitiationType == LLIMMgr::INVITATION_TYPE_IMMEDIATE)
				&& gIMMgr->hasSession(mSessionID))
			{
				// always open IM window when connecting to voice
				LLFloaterChatterBox::showInstance(TRUE);
			}

			gIMMgr->clearPendingAgentListUpdates(mSessionID);
			gIMMgr->clearPendingInvitation(mSessionID);
		}
	}

	/*virtual*/ void error(U32 statusNum, const std::string& reason)
	{
		llwarns << "LLViewerChatterBoxInvitationAcceptResponder error [status:"
				<< statusNum << "]: " << reason << llendl;
		//throw something back to the viewer here?
		if ( gIMMgr )
		{
			gIMMgr->clearPendingAgentListUpdates(mSessionID);
			gIMMgr->clearPendingInvitation(mSessionID);

			LLFloaterIMPanel* floaterp = gIMMgr->findFloaterBySession(mSessionID);

			if ( floaterp )
			{
				if ( 404 == statusNum )
				{
					std::string error_string;
					error_string = "session_does_not_exist_error";

					floaterp->showSessionStartError(error_string);
				}
			}
		}
	}

	/*virtual*/ AIHTTPTimeoutPolicy const& getHTTPTimeoutPolicy(void) const { return viewerChatterBoxInvitationAcceptResponder_timeout; }
	/*virtual*/ char const* getName(void) const { return "LLViewerChatterBoxInvitationAcceptResponder"; }

private:
	LLUUID mSessionID;
	LLIMMgr::EInvitationType mInvitiationType;
};


// the other_participant_id is either an agent_id, a group_id, or an inventory
// folder item_id (collection of calling cards)

// static
LLUUID LLIMMgr::computeSessionID(
	EInstantMessage dialog,
	const LLUUID& other_participant_id)
{
	LLUUID session_id;
	if (IM_SESSION_GROUP_START == dialog)
	{
		// slam group session_id to the group_id (other_participant_id)
		session_id = other_participant_id;
	}
	else if (IM_SESSION_CONFERENCE_START == dialog)
	{
		session_id.generate();
	}
	else if (IM_SESSION_INVITE == dialog)
	{
		// use provided session id for invites
		session_id = other_participant_id;
	}
	else
	{
		LLUUID agent_id = gAgent.getID();
		if (other_participant_id == agent_id)
		{
			// if we try to send an IM to ourselves then the XOR would be null
			// so we just make the session_id the same as the agent_id
			session_id = agent_id;
		}
		else
		{
			// peer-to-peer or peer-to-asset session_id is the XOR
			session_id = other_participant_id ^ agent_id;
		}
	}
	return session_id;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLFloaterIM
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

LLFloaterIM::LLFloaterIM() 
{
	// autoresize=false is necessary to avoid resizing of the IM window whenever 
	// a session is opened or closed (it would otherwise resize the window to match
	// the size of the im-sesssion when they were created.  This happens in 
	// LLMultiFloater::resizeToContents() when called through LLMultiFloater::addFloater())
	this->mAutoResize = FALSE;
	LLUICtrlFactory::getInstance()->buildFloater(this, "floater_im.xml");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLIMViewFriendObserver
//
// Bridge to suport knowing when the inventory has changed.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLIMViewFriendObserver : public LLFriendObserver
{
public:
	LLIMViewFriendObserver(LLIMMgr* tv) : mTV(tv) {}
	virtual ~LLIMViewFriendObserver() {}
	virtual void changed(U32 mask)
	{
		if(mask & (LLFriendObserver::ADD | LLFriendObserver::REMOVE | LLFriendObserver::ONLINE))
		{
			mTV->refresh();
		}
	}
protected:
	LLIMMgr* mTV;
};


bool inviteUserResponse(const LLSD& notification, const LLSD& response)
{
	if (!gIMMgr)
		return false;

	const LLSD& payload = notification["payload"];
	LLUUID session_id = payload["session_id"].asUUID();
	EInstantMessage type = (EInstantMessage)payload["type"].asInteger();
	LLIMMgr::EInvitationType inv_type = (LLIMMgr::EInvitationType)payload["inv_type"].asInteger();
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	switch(option) 
	{
	case 0: // accept
		{
			if (type == IM_SESSION_P2P_INVITE)
			{
				// create a normal IM session
				session_id = gIMMgr->addP2PSession(
					payload["session_name"].asString(),
					payload["caller_id"].asUUID(),
					payload["session_handle"].asString(),
					payload["session_uri"].asString());

				gIMMgr->startCall(session_id);

				// always open IM window when connecting to voice
				LLFloaterChatterBox::showInstance(session_id);

				gIMMgr->clearPendingAgentListUpdates(session_id);
				gIMMgr->clearPendingInvitation(session_id);
			}
			else
			{
				gIMMgr->addSession(
					payload["session_name"].asString(),
					type,
					session_id);

				std::string url = gAgent.getRegion()->getCapability(
					"ChatSessionRequest");

				LLSD data;
				data["method"] = "accept invitation";
				data["session-id"] = session_id;
				LLHTTPClient::post(
					url,
					data,
					new LLViewerChatterBoxInvitationAcceptResponder(
						session_id,
						inv_type));
			}
		}
		break;
	case 2: // mute (also implies ignore, so this falls through to the "ignore" case below)
	{
		// mute the sender of this invite
		if (!LLMuteList::getInstance()->isMuted(payload["caller_id"].asUUID()))
		{
			LLMute mute(payload["caller_id"].asUUID(), payload["caller_name"].asString(), LLMute::AGENT);
			LLMuteList::getInstance()->add(mute);
		}
	}
	/* FALLTHROUGH */
	
	case 1: // decline
	{
		if (type == IM_SESSION_P2P_INVITE)
		{
			std::string s = payload["session_handle"].asString();
			LLVoiceClient::getInstance()->declineInvite(s);
		}
		else
		{
			std::string url = gAgent.getRegion()->getCapability(
				"ChatSessionRequest");

			LLSD data;
			data["method"] = "decline invitation";
			data["session-id"] = session_id;
			LLHTTPClient::post(
				url,
				data,
				new LLHTTPClient::ResponderIgnore);
		}
	}

	gIMMgr->clearPendingAgentListUpdates(session_id);
	gIMMgr->clearPendingInvitation(session_id);
	break;
	}
	
	return false;
}

//
// Public Static Member Functions
//

// This is a helper function to determine what kind of im session
// should be used for the given agent.
// static
EInstantMessage LLIMMgr::defaultIMTypeForAgent(const LLUUID& agent_id)
{
	EInstantMessage type = IM_NOTHING_SPECIAL;
	if (LLAvatarActions::isFriend(agent_id))
	{
		if(LLAvatarTracker::instance().isBuddyOnline(agent_id))
		{
			type = IM_SESSION_CONFERENCE_START;
		}
	}
	return type;
}

// static
//void LLIMMgr::onPinButton(void*)
//{
//	BOOL state = gSavedSettings.getBOOL( "PinTalkViewOpen" );
//	gSavedSettings.setBOOL( "PinTalkViewOpen", !state );
//}

// static 
void LLIMMgr::toggle(void*)
{
	static BOOL return_to_mouselook = FALSE;

	// Hide the button and show the floater or vice versa.
	llassert( gIMMgr );
	BOOL old_state = gIMMgr->getFloaterOpen();
	
	// If we're in mouselook and we triggered the Talk View, we want to talk.
	if( gAgentCamera.cameraMouselook() && old_state )
	{
		return_to_mouselook = TRUE;
		gAgentCamera.changeCameraToDefault();
		return;
	}

	BOOL new_state = !old_state;

	if (new_state)
	{
		// ...making visible
		if ( gAgentCamera.cameraMouselook() )
		{
			return_to_mouselook = TRUE;
			gAgentCamera.changeCameraToDefault();
		}
	}
	else
	{
		// ...hiding
		if ( gAgentCamera.cameraThirdPerson() && return_to_mouselook )
		{
			gAgentCamera.changeCameraToMouselook();
		}
		return_to_mouselook = FALSE;
	}

	gIMMgr->setFloaterOpen( new_state );
}

//
// Member Functions
//

LLIMMgr::LLIMMgr() :
	mFriendObserver(NULL),
	mIMReceived(FALSE),
	mIMUnreadCount(0)
{
	mFriendObserver = new LLIMViewFriendObserver(this);
	LLAvatarTracker::instance().addObserver(mFriendObserver);

	// *HACK: use floater to initialize string constants from xml file
	// then delete it right away
	LLFloaterIM* dummy_floater = new LLFloaterIM();
	delete dummy_floater;

	mPendingInvitations = LLSD::emptyMap();
	mPendingAgentListUpdates = LLSD::emptyMap();
}

LLIMMgr::~LLIMMgr()
{
	LLAvatarTracker::instance().removeObserver(mFriendObserver);
	delete mFriendObserver;
	// Children all cleaned up by default view destructor.
}


extern bool xantispam_check(const std::string&, const std::string&, const std::string&);

// Add a message to a session. 
void LLIMMgr::addMessage(
	const LLUUID& session_id,
	const LLUUID& target_id,
	const std::string& from,
	const std::string& msg,
	const std::string& session_name,
	EInstantMessage dialog,
	U32 parent_estate_id,
	const LLUUID& region_id,
	const LLVector3& position,
	bool link_name) // If this is true, then we insert the name and link it to a profile
{
	LLUUID other_participant_id = target_id;

	// don't process muted IMs
	if (LLMuteList::getInstance()->isMuted(
			other_participant_id,
			LLMute::flagTextChat) && !LLMuteList::getInstance()->isLinden(from))
	{
		return;
	}

	LLFloaterIMPanel* floater;
	LLUUID new_session_id = session_id;
	if (new_session_id.isNull())
	{
		//no session ID...compute new one
		new_session_id = computeSessionID(dialog, other_participant_id);
	}
	floater = findFloaterBySession(new_session_id);
	if (!floater)
	{
		floater = findFloaterBySession(other_participant_id);
		if (floater)
		{
			llinfos << "found the IM session " << session_id 
				<< " by participant " << other_participant_id << llendl;
		}
	}

	if (LLVOAvatar* from_avatar = find_avatar_from_object(target_id)) from_avatar->mIdleTimer.reset(); // Not idle, message sent to somewhere

	// create IM window as necessary
	if(!floater)
	{
		if (gIMMgr->getIgnoreGroupListCount() > 0 && gAgent.isInGroup(session_id))
		{
			// Check to see if we're blocking this group's chat
			LLGroupData* group_data = NULL;
			
			// Search for this group in the agent's groups list
			LLDynamicArray<LLGroupData>::iterator i;

			for (i = gAgent.mGroups.begin(); i != gAgent.mGroups.end(); i++)
			{
				if (i->mID == session_id)
				{
					group_data = &*i;
					break;
				}
			}

			// If the group is in our list then return
			if (group_data && gIMMgr->getIgnoreGroup(group_data->mID))
			{
				return;
			}
		}

		std::string name = from;
		if(!session_name.empty() && session_name.size()>1)
		{
			name = session_name;
		}

		
		floater = createFloater(
			new_session_id,
			other_participant_id,
			name,
			dialog,
			FALSE);

		// When we get a new IM, and if you are a god, display a bit
		// of information about the source. This is to help liaisons
		// when answering questions.
		if(gAgent.isGodlike())
		{
			std::ostringstream bonus_info;
			bonus_info << LLTrans::getString("***")+ " "+ LLTrans::getString("IMParentEstate") + LLTrans::getString(":") + " "
				<< parent_estate_id
				<< ((parent_estate_id == 1) ? LLTrans::getString(",") + LLTrans::getString("IMMainland") : "")
				<< ((parent_estate_id == 5) ? LLTrans::getString(",") + LLTrans::getString ("IMTeen") : "");

			// once we have web-services (or something) which returns
			// information about a region id, we can print this out
			// and even have it link to map-teleport or something.
			//<< "*** region_id: " << region_id << std::endl
			//<< "*** position: " << position << std::endl;

			floater->addHistoryLine(bonus_info.str(), gSavedSettings.getColor4("SystemChatColor"));
		}

		// see if sound or program execution is desired on new sessions
		if(xantispam_check(other_participant_id.asString(), (floater->isGroupSessionType() ? "&-GRNewSessionNoSnd" : "&-IMNewSessionNoSnd"), name))
		{
			make_ui_sound("UISndNewIncomingIMSession");
		}
		xantispam_check(other_participant_id.asString(), (floater->isGroupSessionType() ? "&-ExecOnNewGRSession!" : "&-ExecOnNewIMSession!"), "New chat session: " + name);
	}

	// now add message to floater
	const LLColor4& color = agent_chat_color(other_participant_id, from, false);
	if ( !link_name )
	{
		floater->addHistoryLine(msg,color); // No name to prepend, so just add the message normally
	}
	else
	{
		if( other_participant_id == session_id )
		{
			// The name can be bogus on InWorldz
			floater->addHistoryLine(msg, color, true, LLUUID::null, from);
		}
		else 
		{
			// Insert linked name to front of message
			floater->addHistoryLine(msg, color, true, other_participant_id, from);
		}
	}

	LLFloaterChatterBox* chat_floater = LLFloaterChatterBox::getInstance(LLSD());

	if( !chat_floater->getVisible() && !floater->getVisible())
	{
		//if the IM window is not open and the floater is not visible (i.e. not torn off)
		LLFloater* previouslyActiveFloater = chat_floater->getActiveFloater();

		// select the newly added floater (or the floater with the new line added to it).
		// it should be there.
		chat_floater->selectFloater(floater);

		//there was a previously unseen IM, make that old tab flashing
		//it is assumed that the most recently unseen IM tab is the one current selected/active
		if ( previouslyActiveFloater && getIMReceived() )
		{
			chat_floater->setFloaterFlashing(previouslyActiveFloater, TRUE);
		}

		//notify of a new IM
		notifyNewIM();
		mIMUnreadCount++;
	}
}

void LLIMMgr::addSystemMessage(const LLUUID& session_id, const std::string& message_name, const LLSD& args)
{
	LLUIString message;
	
	// null session id means near me (chat history)
	if (session_id.isNull())
	{
		message = LLTrans::getString(message_name);
		message.setArgs(args);

		LLChat chat(message);
		chat.mSourceType = CHAT_SOURCE_SYSTEM;

		LLFloaterChat::getInstance()->addChatHistory(chat);
	}
	else // going to IM session
	{
		message = LLTrans::getString(message_name + "-im");
		message.setArgs(args);
		if (hasSession(session_id))
		{
			gIMMgr->addMessage(session_id, LLUUID::null, SYSTEM_FROM, message.getString());
		}
	}
}

void LLIMMgr::notifyNewIM()
{
	if(!gIMMgr->getFloaterOpen())
	{
		mIMReceived = TRUE;
	}
}

void LLIMMgr::clearNewIMNotification()
{
	mIMReceived = FALSE;
	mIMUnreadCount = 0;
}

BOOL LLIMMgr::getIMReceived() const
{
	return mIMReceived;
}

int LLIMMgr::getIMUnreadCount()
{
	return mIMUnreadCount;
}

// This method returns TRUE if the local viewer has a session
// currently open keyed to the uuid.
BOOL LLIMMgr::isIMSessionOpen(const LLUUID& uuid)
{
	LLFloaterIMPanel* floater = findFloaterBySession(uuid);
	if(floater) return TRUE;
	return FALSE;
}

void LLIMMgr::autoStartCallOnStartup(const LLUUID& session_id)
{
	// Singu TODO: LLIMModel
	LLFloaterIMPanel* floater = findFloaterBySession(session_id);
	if (!floater) return;

	if (floater->getSessionInitialized())
	{
		startCall(session_id);
	}
	else
	{
		floater->mStartCallOnInitialize = true;
	}
}

LLUUID LLIMMgr::addP2PSession(const std::string& name,
							const LLUUID& other_participant_id,
							const std::string& voice_session_handle,
							const std::string& caller_uri)
{
	LLUUID session_id = addSession(name, IM_NOTHING_SPECIAL, other_participant_id);

	LLFloaterIMPanel* floater = findFloaterBySession(session_id);
	if (floater)
	{
		LLVoiceChannelP2P* voice_channel = dynamic_cast<LLVoiceChannelP2P*>(floater->getVoiceChannel());
		if (voice_channel)
		{
			voice_channel->setSessionHandle(voice_session_handle, caller_uri);
		}
	}
	return session_id;
}

// This adds a session to the talk view. The name is the local name of
// the session, dialog specifies the type of session. If the session
// exists, it is brought forward.  Specifying id = NULL results in an
// im session to everyone. Returns the uuid of the session.
LLUUID LLIMMgr::addSession(
	const std::string& name,
	EInstantMessage dialog,
	const LLUUID& other_participant_id)
{
	LLUUID session_id = computeSessionID(dialog, other_participant_id);

	LLFloaterIMPanel* floater = findFloaterBySession(session_id);
	if(!floater)
	{
		LLDynamicArray<LLUUID> ids;
		ids.put(other_participant_id);

		floater = createFloater(
			session_id,
			other_participant_id,
			name,
			ids,
			dialog,
			TRUE);

		noteOfflineUsers(floater, ids);
		LLFloaterChatterBox::showInstance(session_id);

		// Only warn for regular IMs - not group IMs
		if( dialog == IM_NOTHING_SPECIAL )
		{
			noteMutedUsers(floater, ids);
		}

		static LLCachedControl<bool> tear_off("OtherChatsTornOff");
		if(tear_off)
		{
			// removal sets up relationship for re-attach
			LLFloaterChatterBox::getInstance(LLSD())->removeFloater(floater);
			// reparent to floater view
			gFloaterView->addChild(floater);
			gFloaterView->bringToFront(floater);
		}
		else
			LLFloaterChatterBox::getInstance(LLSD())->showFloater(floater);
	}
	else
	{
		floater->open();
	}
	//mTabContainer->selectTabPanel(panel);
	if(gSavedSettings.getBOOL("PhoenixIMAnnounceStealFocus"))
	{	
		floater->setInputFocus(TRUE);
	}
	return floater->getSessionID();
}

// Adds a session using the given session_id.  If the session already exists 
// the dialog type is assumed correct. Returns the uuid of the session.
LLUUID LLIMMgr::addSession(
	const std::string& name,
	EInstantMessage dialog,
	const LLUUID& other_participant_id,
	const LLDynamicArray<LLUUID>& ids)
{
	if (0 == ids.getLength())
	{
		return LLUUID::null;
	}

	LLUUID session_id = computeSessionID(
		dialog,
		other_participant_id);

	LLFloaterIMPanel* floater = findFloaterBySession(session_id);
	if(!floater)
	{
		// On creation, use the first element of ids as the
		// "other_participant_id"
		floater = createFloater(
			session_id,
			other_participant_id,
			name,
			ids,
			dialog,
			TRUE);

		if ( !floater ) return LLUUID::null;

		noteOfflineUsers(floater, ids);
		LLFloaterChatterBox::showInstance(session_id);

		// Only warn for regular IMs - not group IMs
		if( dialog == IM_NOTHING_SPECIAL )
		{
			noteMutedUsers(floater, ids);
		}
	}
	else
	{
		floater->open();
	}
	//mTabContainer->selectTabPanel(panel);
	if(gSavedSettings.getBOOL("PhoenixIMAnnounceStealFocus"))
	{
		floater->setInputFocus(TRUE);
	}
	return floater->getSessionID();
}

// This removes the panel referenced by the uuid, and then restores
// internal consistency. The internal pointer is not deleted.
void LLIMMgr::removeSession(const LLUUID& session_id)
{
	LLFloaterIMPanel* floater = findFloaterBySession(session_id);
	if(floater)
	{
		mFloaters.erase(floater->getHandle());
		LLFloaterChatterBox::getInstance(LLSD())->removeFloater(floater);
		//mTabContainer->removeTabPanel(floater);

		clearPendingInvitation(session_id);
		clearPendingAgentListUpdates(session_id);
	}
}

void LLIMMgr::inviteToSession(
	const LLUUID& session_id, 
	const std::string& session_name, 
	const LLUUID& caller_id, 
	const std::string& caller_name,
	EInstantMessage type,
	EInvitationType inv_type,
	const std::string& session_handle,
	const std::string& session_uri)
{
	//ignore invites from muted residents
	if (LLMuteList::getInstance()->isMuted(caller_id))
	{
		return;
	}

	std::string notify_box_type;

	BOOL ad_hoc_invite = FALSE;
	if(type == IM_SESSION_P2P_INVITE)
	{
		//P2P is different...they only have voice invitations
		notify_box_type = "VoiceInviteP2P";
	}
	else if ( gAgent.isInGroup(session_id) )
	{
		//only really old school groups have voice invitations
		notify_box_type = "VoiceInviteGroup";
	}
	else if ( inv_type == INVITATION_TYPE_VOICE )
	{
		//else it's an ad-hoc
		//and a voice ad-hoc
		notify_box_type = "VoiceInviteAdHoc";
		ad_hoc_invite = TRUE;
	}
	else if ( inv_type == INVITATION_TYPE_IMMEDIATE )
	{
		notify_box_type = "InviteAdHoc";
		ad_hoc_invite = TRUE;
	}

	LLSD payload;
	payload["session_id"] = session_id;
	payload["session_name"] = session_name;
	payload["caller_id"] = caller_id;
	payload["caller_name"] = caller_name;
	payload["type"] = type;
	payload["inv_type"] = inv_type;
	payload["session_handle"] = session_handle;
	payload["session_uri"] = session_uri;
	payload["notify_box_type"] = notify_box_type;

	LLVoiceChannel* channelp = LLVoiceChannel::getChannelByID(session_id);
	if (channelp && channelp->callStarted())
	{
		// you have already started a call to the other user, so just accept the invite
		LLNotifications::instance().forceResponse(LLNotification::Params("VoiceInviteP2P").payload(payload), 0);
		return;
	}

	if (type == IM_SESSION_P2P_INVITE || ad_hoc_invite)
	{
		if	(	// we're rejecting non-friend voice calls and this isn't a friend
				(gSavedSettings.getBOOL("VoiceCallsFriendsOnly") && (LLAvatarTracker::instance().getBuddyInfo(caller_id) == NULL))
			)
		{
			// silently decline the call
			LLNotifications::instance().forceResponse(LLNotification::Params("VoiceInviteP2P").payload(payload), 1);
			return;
		}
	}

	if ( !mPendingInvitations.has(session_id.asString()) )
	{
		if (caller_name.empty())
		{
			gCacheName->get(caller_id, true,  // voice
				boost::bind(&LLIMMgr::onInviteNameLookup, _1, _2, _3, payload));
		}
		else
		{
			LLSD args;
			args["NAME"] = caller_name;
			args["GROUP"] = session_name;

			LLNotifications::instance().add(notify_box_type, 
					     args, 
						 payload,
						 &inviteUserResponse);

		}
		mPendingInvitations[session_id.asString()] = LLSD();
	}
}

//static
void LLIMMgr::onInviteNameLookup(const LLUUID& id, const std::string& full_name, bool is_group, LLSD payload)
{
	payload["caller_name"] = full_name;
	payload["session_name"] = full_name;

	LLSD args;
	args["NAME"] = full_name;

	LLNotifications::instance().add(
		payload["notify_box_type"].asString(),
		args, 
		payload,
		&inviteUserResponse);
}

void LLIMMgr::refresh()
{
}

void LLIMMgr::setFloaterOpen(BOOL set_open)
{
	if (set_open)
	{
		LLFloaterChatterBox::showInstance();
	}
	else
	{
		LLFloaterChatterBox::hideInstance();
	}
}


BOOL LLIMMgr::getFloaterOpen()
{
	return LLFloaterChatterBox::instanceVisible(LLSD());
}
 
void LLIMMgr::disconnectAllSessions()
{
	LLFloaterIMPanel* floater = NULL;
	std::set<LLHandle<LLFloater> >::iterator handle_it;
	for(handle_it = mFloaters.begin();
		handle_it != mFloaters.end();
		)
	{
		floater = (LLFloaterIMPanel*)handle_it->get();

		// MUST do this BEFORE calling floater->onClose() because that may remove the item from the set, causing the subsequent increment to crash.
		++handle_it;

		if (floater)
		{
			floater->setEnabled(FALSE);
			floater->close(TRUE);
		}
	}
}


// This method returns the im panel corresponding to the uuid
// provided. The uuid can either be a session id or an agent
// id. Returns NULL if there is no matching panel.
LLFloaterIMPanel* LLIMMgr::findFloaterBySession(const LLUUID& session_id)
{
	LLFloaterIMPanel* rv = NULL;
	std::set<LLHandle<LLFloater> >::iterator handle_it;
	for(handle_it = mFloaters.begin();
		handle_it != mFloaters.end();
		++handle_it)
	{
		rv = (LLFloaterIMPanel*)handle_it->get();
		if(rv && session_id == rv->getSessionID())
		{
			break;
		}
		rv = NULL;
	}
	return rv;
}


BOOL LLIMMgr::hasSession(const LLUUID& session_id)
{
	return (findFloaterBySession(session_id) != NULL);
}

void LLIMMgr::clearPendingInvitation(const LLUUID& session_id)
{
	if ( mPendingInvitations.has(session_id.asString()) )
	{
		mPendingInvitations.erase(session_id.asString());
	}
}

void LLIMMgr::processAgentListUpdates(const LLUUID& session_id, const LLSD& body)
{
	LLFloaterIMPanel* im_floater = gIMMgr->findFloaterBySession(session_id);
	if (!im_floater) return;
	LLIMSpeakerMgr* speaker_mgr = im_floater->getSpeakerManager();
	if (speaker_mgr)
	{
		speaker_mgr->updateSpeakers(body);

		// also the same call is added into LLVoiceClient::participantUpdatedEvent because
		// sometimes it is called AFTER LLViewerChatterBoxSessionAgentListUpdates::post()
		// when moderation state changed too late. See EXT-3544.
		speaker_mgr->update(true);
	}
	else
	{
		//we don't have a speaker manager yet..something went wrong
		//we are probably receiving an update here before
		//a start or an acceptance of an invitation.  Race condition.
		gIMMgr->addPendingAgentListUpdates(
			session_id,
			body);
	}
}

LLSD LLIMMgr::getPendingAgentListUpdates(const LLUUID& session_id)
{
	if ( mPendingAgentListUpdates.has(session_id.asString()) )
	{
		return mPendingAgentListUpdates[session_id.asString()];
	}
	else
	{
		return LLSD();
	}
}

void LLIMMgr::addPendingAgentListUpdates(
	const LLUUID& session_id,
	const LLSD& updates)
{
	LLSD::map_const_iterator iter;

	if ( !mPendingAgentListUpdates.has(session_id.asString()) )
	{
		//this is a new agent list update for this session
		mPendingAgentListUpdates[session_id.asString()] = LLSD::emptyMap();
	}

	if (
		updates.has("agent_updates") &&
		updates["agent_updates"].isMap() &&
		updates.has("updates") &&
		updates["updates"].isMap() )
	{
		//new school update
		LLSD update_types = LLSD::emptyArray();
		LLSD::array_iterator array_iter;

		update_types.append("agent_updates");
		update_types.append("updates");

		for (
			array_iter = update_types.beginArray();
			array_iter != update_types.endArray();
			++array_iter)
		{
			//we only want to include the last update for a given agent
			for (
				iter = updates[array_iter->asString()].beginMap();
				iter != updates[array_iter->asString()].endMap();
				++iter)
			{
				mPendingAgentListUpdates[session_id.asString()][array_iter->asString()][iter->first] =
					iter->second;
			}
		}
	}
	else if (
		updates.has("updates") &&
		updates["updates"].isMap() )
	{
		//old school update where the SD contained just mappings
		//of agent_id -> "LEAVE"/"ENTER"

		//only want to keep last update for each agent
		for (
			iter = updates["updates"].beginMap();
			iter != updates["updates"].endMap();
			++iter)
		{
			mPendingAgentListUpdates[session_id.asString()]["updates"][iter->first] =
				iter->second;
		}
	}
}

void LLIMMgr::clearPendingAgentListUpdates(const LLUUID& session_id)
{
	if ( mPendingAgentListUpdates.has(session_id.asString()) )
	{
		mPendingAgentListUpdates.erase(session_id.asString());
	}
}

bool LLIMMgr::startCall(const LLUUID& session_id, LLVoiceChannel::EDirection direction)
{
	// Singu TODO: LLIMModel
	LLFloaterIMPanel* floater = gIMMgr->findFloaterBySession(session_id);
	if (!floater) return false;

	LLVoiceChannel* voice_channel = floater->getVoiceChannel();
	if (!voice_channel) return false;

	voice_channel->setCallDirection(direction);
	voice_channel->activate();
	return true;
}

bool LLIMMgr::endCall(const LLUUID& session_id)
{
	// Singu TODO: LLIMModel
	LLFloaterIMPanel* floater = gIMMgr->findFloaterBySession(session_id);
	if (!floater) return false;

	LLVoiceChannel* voice_channel = floater->getVoiceChannel();
	if (!voice_channel) return false;

	voice_channel->deactivate();
	/*LLIMModel::LLIMSession* im_session = LLIMModel::getInstance()->findIMSession(session_id);
	if (im_session)*/
	{
		// need to update speakers' state
		floater->getSpeakerManager()->update(FALSE);
	}
	return true;
}

// create a floater and update internal representation for
// consistency. Returns the pointer, caller (the class instance since
// it is a private method) is not responsible for deleting the
// pointer.  Add the floater to this but do not select it.
LLFloaterIMPanel* LLIMMgr::createFloater(
	const LLUUID& session_id,
	const LLUUID& other_participant_id,
	const std::string& session_label,
	EInstantMessage dialog,
	BOOL user_initiated)
{
	if (session_id.isNull())
	{
		llwarns << "Creating LLFloaterIMPanel with null session ID" << llendl;
	}

	llinfos << "LLIMMgr::createFloater: from " << other_participant_id 
			<< " in session " << session_id << llendl;
	LLFloaterIMPanel* floater = new LLFloaterIMPanel(session_label,
													 session_id,
													 other_participant_id,
													 dialog);
	LLTabContainer::eInsertionPoint i_pt = user_initiated ? LLTabContainer::RIGHT_OF_CURRENT : LLTabContainer::END;
	LLFloaterChatterBox::getInstance(LLSD())->addFloater(floater, FALSE, i_pt);
	static LLCachedControl<bool> tear_off("OtherChatsTornOff");
	if (tear_off)
	{
		LLFloaterChatterBox::getInstance(LLSD())->removeFloater(floater); // removal sets up relationship for re-attach
		gFloaterView->addChild(floater); // reparent to floater view
		LLFloater* focused_floater = gFloaterView->getFocusedFloater(); // obtain the focused floater
		floater->open(); // make the new chat floater appear
		static LLCachedControl<bool> minimize("OtherChatsTornOffAndMinimized");
		if (focused_floater != NULL) // there was a focused floater
		{
			floater->setMinimized(minimize); // so minimize this one, for now, if desired
			focused_floater->setFocus(true); // and work around focus being removed by focusing on the last
		}
		else if (minimize)
		{
			floater->setFocus(false); // work around focus being granted to new floater
			floater->setMinimized(true);
		}
	}
	mFloaters.insert(floater->getHandle());
	return floater;
}

LLFloaterIMPanel* LLIMMgr::createFloater(
	const LLUUID& session_id,
	const LLUUID& other_participant_id,
	const std::string& session_label,
	const LLDynamicArray<LLUUID>& ids,
	EInstantMessage dialog,
	BOOL user_initiated)
{
	if (session_id.isNull())
	{
		llwarns << "Creating LLFloaterIMPanel with null session ID" << llendl;
	}

	llinfos << "LLIMMgr::createFloater: from " << other_participant_id 
			<< " in session " << session_id << llendl;
	LLFloaterIMPanel* floater = new LLFloaterIMPanel(session_label,
													 session_id,
													 other_participant_id,
													 ids,
													 dialog);
	LLTabContainer::eInsertionPoint i_pt = user_initiated ? LLTabContainer::RIGHT_OF_CURRENT : LLTabContainer::END;
	LLFloaterChatterBox::getInstance(LLSD())->addFloater(floater, FALSE, i_pt);
	static LLCachedControl<bool> tear_off("OtherChatsTornOff");
	if (tear_off)
	{
		LLFloaterChatterBox::getInstance(LLSD())->removeFloater(floater); // removal sets up relationship for re-attach
		gFloaterView->addChild(floater); // reparent to floater view
		LLFloater* focused_floater = gFloaterView->getFocusedFloater(); // obtain the focused floater
		floater->open(); // make the new chat floater appear
		static LLCachedControl<bool> minimize("OtherChatsTornOffAndMinimized");
		if (focused_floater != NULL) // there was a focused floater
		{
			floater->setMinimized(minimize); // so minimize this one, for now, if desired
			focused_floater->setFocus(true); // and work around focus being removed by focusing on the last
		}
		else if (minimize)
		{
			floater->setFocus(false); // work around focus being granted to new floater
			floater->setMinimized(true);
		}
	}
	mFloaters.insert(floater->getHandle());
	return floater;
}

void LLIMMgr::noteOfflineUsers(
	LLFloaterIMPanel* floater,
	const LLDynamicArray<LLUUID>& ids)
{
	S32 count = ids.count();
	if(count == 0)
	{
		const std::string& only_user = LLTrans::getString("only_user_message");
		floater->addHistoryLine(only_user, gSavedSettings.getColor4("SystemChatColor"));
	}
	else
	{
		const LLRelationship* info = NULL;
		LLAvatarTracker& at = LLAvatarTracker::instance();
		for(S32 i = 0; i < count; ++i)
		{
			info = at.getBuddyInfo(ids.get(i));
			std::string full_name;
			if (info
				&& !info->isOnline()
				&& LLAvatarNameCache::getPNSName(ids.get(i), full_name))
			{
				LLUIString offline = LLTrans::getString("offline_message");
				offline.setArg("[NAME]", full_name);
				floater->addHistoryLine(offline, gSavedSettings.getColor4("SystemChatColor"));
			}
		}
	}
}

void LLIMMgr::noteMutedUsers(LLFloaterIMPanel* floater,
								  const LLDynamicArray<LLUUID>& ids)
{
	// Don't do this if we don't have a mute list.
	LLMuteList *ml = LLMuteList::getInstance();
	if( !ml )
	{
		return;
	}

	S32 count = ids.count();
	if(count > 0)
	{
		for(S32 i = 0; i < count; ++i)
		{
			if( ml->isMuted(ids.get(i)) )
			{
				LLUIString muted = LLTrans::getString("muted_message");

				floater->addHistoryLine(muted);
				break;
			}
		}
	}
}

void LLIMMgr::processIMTypingStart(const LLIMInfo* im_info)
{
	processIMTypingCore(im_info, TRUE);
}

void LLIMMgr::processIMTypingStop(const LLIMInfo* im_info)
{
	processIMTypingCore(im_info, FALSE);
}

void LLIMMgr::processIMTypingCore(const LLIMInfo* im_info, BOOL typing)
{
	LLUUID session_id = computeSessionID(im_info->mIMType, im_info->mFromID);
	LLFloaterIMPanel* floater = findFloaterBySession(session_id);
	if (floater)
	{
		floater->processIMTyping(im_info, typing);
	}
}

void LLIMMgr::updateFloaterSessionID(
	const LLUUID& old_session_id,
	const LLUUID& new_session_id)
{
	LLFloaterIMPanel* floater = findFloaterBySession(old_session_id);
	if (floater)
	{
		floater->sessionInitReplyReceived(new_session_id);
	}
}

void LLIMMgr::loadIgnoreGroup()
{
	std::string filename = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "ignore_groups.xml");

	LLSD settings_llsd;
	llifstream file;
	file.open(filename);
	if (file.is_open())
	{
		// llinfos << "loading group chat ignore from " << filename << "..." << llendl;
		LLSDSerialize::fromXML(settings_llsd, file);

		mIgnoreGroupList.clear();

		for(LLSD::array_const_iterator iter = settings_llsd.beginArray();
		    iter != settings_llsd.endArray(); ++iter)
		{
			// llinfos << "added " << iter->asUUID()
			//         << " to group chat ignore list" << llendl;
			mIgnoreGroupList.push_back( iter->asUUID() );
		}
	}
	else
	{
		// llinfos << "can't load " << filename
		//         << " (probably it doesn't exist yet)" << llendl;
	}
}

void LLIMMgr::saveIgnoreGroup()
{
	// llinfos << "saving ignore_groups.xml" << llendl;

	std::string user_dir = gDirUtilp->getLindenUserDir(true);
	if (!user_dir.empty())
	{
		std::string filename = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "ignore_groups.xml");

		LLSD settings_llsd = LLSD::emptyArray();

		for(std::list<LLUUID>::iterator iter = mIgnoreGroupList.begin(); 
		    iter != mIgnoreGroupList.end(); ++iter)
		{
			settings_llsd.append(*iter);
		}

		llofstream file;
		file.open(filename);
		LLSDSerialize::toPrettyXML(settings_llsd, file);
	}
}

void LLIMMgr::updateIgnoreGroup(const LLUUID& group_id, bool ignore)
{
	if (group_id.isNull()) return;

	if (getIgnoreGroup(group_id) == ignore)
	{
		// nothing to do
		// llinfos << "no change to group " << group_id << ", it is already "
		//         << (ignore ? "" : "not ") << "ignored" << llendl;
		return;
	}
	else if (!ignore)
	{
		// change from ignored to not ignored
		// llinfos << "unignoring group " << group_id << llendl;
		mIgnoreGroupList.remove(group_id);
	}
	else //if (ignore)
	{
		// change from not ignored to ignored
		// llinfos << "ignoring group " << group_id << llendl;
		mIgnoreGroupList.push_back(group_id);
	}
}

bool LLIMMgr::getIgnoreGroup(const LLUUID& group_id) const
{
	if (group_id.notNull())
	{
		std::list<LLUUID>::const_iterator found =
			std::find( mIgnoreGroupList.begin(), mIgnoreGroupList.end(),
			           group_id);

		if (found != mIgnoreGroupList.end())
		{
			// llinfos << "group " << group_id << " is ignored." << llendl;
			return true;
		}
	}
	// llinfos << "group " << group_id << " is not ignored." << llendl;
	return false;
}


//////////////////////
///// ChatterBox /////
//////////////////////


LLFloaterChatterBox* LLIMMgr::getFloater()
{ 
	return LLFloaterChatterBox::getInstance(LLSD()); 
}

class LLViewerChatterBoxSessionStartReply : public LLHTTPNode
{
public:
	virtual void describe(Description& desc) const
	{
		desc.shortInfo("Used for receiving a reply to a request to initialize an ChatterBox session");
		desc.postAPI();
		desc.input(
			"{\"client_session_id\": UUID, \"session_id\": UUID, \"success\" boolean, \"reason\": string");
		desc.source(__FILE__, __LINE__);
	}

	virtual void post(ResponsePtr response,
					  const LLSD& context,
					  const LLSD& input) const
	{
		LLSD body;
		LLUUID temp_session_id;
		LLUUID session_id;
		bool success;

		body = input["body"];
		success = body["success"].asBoolean();
		temp_session_id = body["temp_session_id"].asUUID();

		if ( success )
		{
			session_id = body["session_id"].asUUID();
			// Singu TODO: LLIMModel
			gIMMgr->updateFloaterSessionID(temp_session_id, session_id);

			LLFloaterIMPanel* im_floater = gIMMgr->findFloaterBySession(session_id);
			LLIMSpeakerMgr* speaker_mgr = im_floater ? im_floater->getSpeakerManager() : NULL;
			if (speaker_mgr)
			{
				speaker_mgr->setSpeakers(body);
				speaker_mgr->updateSpeakers(gIMMgr->getPendingAgentListUpdates(session_id));
			}

			if (im_floater)
			{
				if ( body.has("session_info") )
				{
					im_floater->processSessionUpdate(body["session_info"]);
				}
			}

			gIMMgr->clearPendingAgentListUpdates(session_id);
		}
		else
		{
			if (LLFloaterIMPanel* floater = gIMMgr->findFloaterBySession(temp_session_id))
			{
				floater->showSessionStartError(body["error"].asString());
			}
		}

		gIMMgr->clearPendingAgentListUpdates(session_id);
	}
};

class LLViewerChatterBoxSessionEventReply : public LLHTTPNode
{
public:
	virtual void describe(Description& desc) const
	{
		desc.shortInfo("Used for receiving a reply to a ChatterBox session event");
		desc.postAPI();
		desc.input(
			"{\"event\": string, \"reason\": string, \"success\": boolean, \"session_id\": UUID");
		desc.source(__FILE__, __LINE__);
	}

	virtual void post(ResponsePtr response,
					  const LLSD& context,
					  const LLSD& input) const
	{
		LLUUID session_id;
		bool success;

		LLSD body = input["body"];
		success = body["success"].asBoolean();
		session_id = body["session_id"].asUUID();

		if ( !success )
		{
			//throw an error dialog
			LLFloaterIMPanel* floater = 
				gIMMgr->findFloaterBySession(session_id);

			if (floater)
			{
				floater->showSessionEventError(
					body["event"].asString(),
					body["error"].asString());
			}
		}
	}
};

class LLViewerForceCloseChatterBoxSession: public LLHTTPNode
{
public:
	virtual void post(ResponsePtr response,
					  const LLSD& context,
					  const LLSD& input) const
	{
		LLUUID session_id;
		std::string reason;

		session_id = input["body"]["session_id"].asUUID();
		reason = input["body"]["reason"].asString();

		LLFloaterIMPanel* floater =
			gIMMgr ->findFloaterBySession(session_id);

		if ( floater )
		{
			floater->showSessionForceClose(reason);
		}
	}
};

class LLViewerChatterBoxSessionAgentListUpdates : public LLHTTPNode
{
public:
	virtual void post(
		ResponsePtr responder,
		const LLSD& context,
		const LLSD& input) const
	{
		const LLUUID& session_id = input["body"]["session_id"].asUUID();
		gIMMgr->processAgentListUpdates(session_id, input["body"]);
	}
};

class LLViewerChatterBoxSessionUpdate : public LLHTTPNode
{
public:
	virtual void post(
		ResponsePtr responder,
		const LLSD& context,
		const LLSD& input) const
	{
		LLUUID session_id = input["body"]["session_id"].asUUID();
		LLFloaterIMPanel* im_floater = gIMMgr->findFloaterBySession(session_id);
		if ( im_floater )
		{
			im_floater->processSessionUpdate(input["body"]["info"]);
		}
		LLIMSpeakerMgr* im_mgr = im_floater ? im_floater->getSpeakerManager() : NULL; //LLIMModel::getInstance()->getSpeakerManager(session_id);
		if (im_mgr)
		{
			im_mgr->processSessionUpdate(input["body"]["info"]);
		}
	}
};


class LLViewerChatterBoxInvitation : public LLHTTPNode
{
public:

	virtual void post(
		ResponsePtr response,
		const LLSD& context,
		const LLSD& input) const
	{
		//for backwards compatiblity reasons...we need to still
		//check for 'text' or 'voice' invitations...bleh
		if ( input["body"].has("instantmessage") )
		{
			LLSD message_params =
				input["body"]["instantmessage"]["message_params"];

			//do something here to have the IM invite behave
			//just like a normal IM
			//this is just replicated code from process_improved_im
			//and should really go in it's own function -jwolk
			if (gNoRender)
			{
				return;
			}
			LLChat chat;

			std::string message = message_params["message"].asString();
			std::string name = message_params["from_name"].asString();
			LLUUID from_id = message_params["from_id"].asUUID();
			LLUUID session_id = message_params["id"].asUUID();
			std::vector<U8> bin_bucket = message_params["data"]["binary_bucket"].asBinary();
			U8 offline = (U8)message_params["offline"].asInteger();
			
			time_t timestamp =
				(time_t) message_params["timestamp"].asInteger();

			BOOL is_busy = gAgent.getBusy();
			BOOL is_muted = LLMuteList::getInstance()->isMuted(
				from_id,
				name,
				LLMute::flagTextChat);

			BOOL is_linden = LLMuteList::getInstance()->isLinden(name);
			std::string separator_string(": ");
			int message_offset=0;

			//Handle IRC styled /me messages.
			std::string prefix = message.substr(0, 4);
			if (prefix == "/me " || prefix == "/me'")
			{
				separator_string = "";
				message_offset = 3;
				chat.mChatStyle = CHAT_STYLE_IRC;
			}

			chat.mMuted = is_muted && !is_linden;
			chat.mFromID = from_id;
			chat.mFromName = name;

			if (!is_linden && (is_busy || is_muted))
			{
				return;
			}

			bool group = gAgent.isInGroup(session_id);
// [RLVa:KB] - Checked: 2010-11-30 (RLVa-1.3.0c) | Modified: RLVa-1.3.0c
			if ( (gRlvHandler.hasBehaviour(RLV_BHVR_RECVIM)) || (gRlvHandler.hasBehaviour(RLV_BHVR_RECVIMFROM)) )
			{
				if (group)												// Group chat: don't accept the invite if not an exception
				{
					if (!gRlvHandler.canReceiveIM(session_id))
						return;
				}
				else if (!gRlvHandler.canReceiveIM(from_id))			// Conference chat: don't block; censor if not an exception
				{
					message = RlvStrings::getString(RLV_STRING_BLOCKED_RECVIM);
				}
			}
// [/RLVa:KB]

			// standard message, not from system
			std::string saved;
			if(offline == IM_OFFLINE)
			{
				LLStringUtil::format_map_t args;
				args["[LONG_TIMESTAMP]"] = formatted_time(timestamp);
				saved = LLTrans::getString("Saved_message", args);
			}
			std::string buffer = separator_string + saved + message.substr(message_offset);

			BOOL is_this_agent = FALSE;
			if(from_id == gAgentID)
			{
				is_this_agent = TRUE;
			}
			gIMMgr->addMessage(
				session_id,
				from_id,
				name,
				buffer,
				std::string((char*)&bin_bucket[0]),
				IM_SESSION_INVITE,
				message_params["parent_estate_id"].asInteger(),
				message_params["region_id"].asUUID(),
				ll_vector3_from_sd(message_params["position"]),
				true);

			std::string prepend_msg;
			if (group)
			{
				if (gIMMgr->getIgnoreGroup(session_id))
				{
					// Tell the server we've left group chat
					std::string name;
					gAgent.buildFullname(name);
					pack_instant_message(gMessageSystem, gAgentID, false, gAgent.getSessionID(), from_id,
						name, LLStringUtil::null, IM_ONLINE, IM_SESSION_LEAVE, session_id);
					gAgent.sendReliableMessage();
					gIMMgr->removeSession(session_id);
					return;
				}
				else if (gSavedSettings.getBOOL("OptionShowGroupNameInChatIM"))
				{
					LLGroupData group_data;
					gAgent.getGroupData(session_id, group_data);
					prepend_msg = "[";
					prepend_msg += group_data.mName;
					prepend_msg += "] ";
				}
			}
			else
			{
				prepend_msg = std::string("IM: ");
			}
			chat.mText = prepend_msg + name + separator_string + saved + message.substr(message_offset);
			LLFloaterChat::addChat(chat, TRUE, is_this_agent);

			//K now we want to accept the invitation
			std::string url = gAgent.getRegion()->getCapability(
				"ChatSessionRequest");

			if ( url != "" )
			{
				LLSD data;
				data["method"] = "accept invitation";
				data["session-id"] = session_id;
				LLHTTPClient::post(
					url,
					data,
					new LLViewerChatterBoxInvitationAcceptResponder(
						session_id,
						LLIMMgr::INVITATION_TYPE_INSTANT_MESSAGE));
			}
		} //end if invitation has instant message
		else if ( input["body"].has("voice") )
		{
			if (gNoRender)
			{
				return;
			}

			if(!LLVoiceClient::getInstance()->voiceEnabled())
			{
				// Don't display voice invites unless the user has voice enabled.
				return;
			}

			gIMMgr->inviteToSession(
				input["body"]["session_id"].asUUID(), 
				input["body"]["session_name"].asString(), 
				input["body"]["from_id"].asUUID(),
				input["body"]["from_name"].asString(),
				IM_SESSION_INVITE,
				LLIMMgr::INVITATION_TYPE_VOICE);
		}
		else if ( input["body"].has("immediate") )
		{
			gIMMgr->inviteToSession(
				input["body"]["session_id"].asUUID(), 
				input["body"]["session_name"].asString(), 
				input["body"]["from_id"].asUUID(),
				input["body"]["from_name"].asString(),
				IM_SESSION_INVITE,
				LLIMMgr::INVITATION_TYPE_IMMEDIATE);
		}
	}
};

LLHTTPRegistration<LLViewerChatterBoxSessionStartReply>
   gHTTPRegistrationMessageChatterboxsessionstartreply(
	   "/message/ChatterBoxSessionStartReply");

LLHTTPRegistration<LLViewerChatterBoxSessionEventReply>
   gHTTPRegistrationMessageChatterboxsessioneventreply(
	   "/message/ChatterBoxSessionEventReply");

LLHTTPRegistration<LLViewerForceCloseChatterBoxSession>
    gHTTPRegistrationMessageForceclosechatterboxsession(
		"/message/ForceCloseChatterBoxSession");

LLHTTPRegistration<LLViewerChatterBoxSessionAgentListUpdates>
    gHTTPRegistrationMessageChatterboxsessionagentlistupdates(
	    "/message/ChatterBoxSessionAgentListUpdates");

LLHTTPRegistration<LLViewerChatterBoxSessionUpdate>
    gHTTPRegistrationMessageChatterBoxSessionUpdate(
	    "/message/ChatterBoxSessionUpdate");

LLHTTPRegistration<LLViewerChatterBoxInvitation>
    gHTTPRegistrationMessageChatterBoxInvitation(
		"/message/ChatterBoxInvitation");

