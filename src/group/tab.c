#include "group.h"

/**
 *
 * Beryl group plugin
 *
 * tab.c
 *
 * Copyright : (C) 2006 by Patrick Niklaus, Roi Cohen, Danny Baumann
 * Authors: Patrick Niklaus <patrick.niklaus@googlemail.com>
 *          Roi Cohen       <roico@beryl-project.org>
 *          Danny Baumann   <maniac@beryl-project.org>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 **/

/*
 * groupGetCurrentMousePosition
 *
 * Description:
 * Return the current function of the pointer at the given screen.
 * The position is queried trough XQueryPointer directly from the xserver.
 *
 */
Bool groupGetCurrentMousePosition(CompScreen *s, int *x, int *y)
{
	unsigned int rmask;
	int mouseX, mouseY, winX, winY;
	Window root;
	Window child;
	Bool result;
	
	result = XQueryPointer (s->display->display, s->root, &root, 
			&child, &mouseX, &mouseY, &winX, &winY, &rmask);
	
	if (result) {
		(*x) = mouseX;
		(*y) = mouseY;
	}

	return result;
}

/*
 * groupGetClippingRegion
 *
 * Description:
 * This function returns a clipping region which is used to clip
 * several events involving window stack such as hover detection
 * in the tab bar or Drag'n'Drop. It creates the clipping region
 * with getting the region of every window above the given window
 * and then adds this region to the clipping region using
 * XUnionRectWithRegion. w->region won't work since it doesn't include
 * the window decoration.
 *
 */
Region groupGetClippingRegion(CompWindow *w)
{
	Region clip = XCreateRegion();
	CompWindow *cw;
	for(cw = w->next; cw; cw = cw->next)
	{
		if (!cw->invisible && !(cw->state & CompWindowStateHiddenMask)) {
			Region buf = XCreateRegion();
			
			XRectangle rect;
			rect.x = WIN_REAL_X(cw);
			rect.y = WIN_REAL_Y(cw);
			rect.width = WIN_REAL_WIDTH(cw);
			rect.height = WIN_REAL_HEIGHT(cw);
			XUnionRectWithRegion(&rect, buf, buf);

			XUnionRegion(clip, buf, clip);
			XDestroyRegion(buf);
		}
	}

	return clip;
}


/*
 * groupClearWindowInputShape
 *
 */
void
groupClearWindowInputShape(CompWindow *w, GroupWindowHideInfo *hideInfo)
{
	XRectangle *rects;
	int count, ordering;

	rects = XShapeGetRectangles(w->screen->display->display, 
			w->id, ShapeInput, &count, &ordering);

	if (count == 0) {
		XFree(rects);
		return;
	}

	/* check if the returned shape exactly matches the window shape -
	   if that is true, the window currently has no set input shape */
	if ((count == 1) &&
		(rects[0].x == -w->serverBorderWidth) &&
		(rects[0].y == -w->serverBorderWidth) &&
		(rects[0].width == (w->serverWidth + w->serverBorderWidth)) &&
		(rects[0].height == (w->serverHeight + w->serverBorderWidth)))
	{
		count = 0;
	}

	if (hideInfo->inputRects)
		XFree(hideInfo->inputRects);

	hideInfo->inputRects = rects;
	hideInfo->nInputRects = count;
	hideInfo->inputRectOrdering = ordering;

	XShapeSelectInput(w->screen->display->display, w->id, NoEventMask);
			
	XShapeCombineRectangles(w->screen->display->display, w->id,
			ShapeInput, 0, 0, NULL, 0, ShapeSet, 0);
 
	XShapeSelectInput(w->screen->display->display, w->id, ShapeNotify);
}

/*
 * groupSetWindowVisibility
 *
 */
void
groupSetWindowVisibility(CompWindow *w, Bool visible)
{
	GROUP_WINDOW(w);

	if (!visible && !gw->windowHideInfo) 
	{
		gw->windowHideInfo = malloc(sizeof(GroupWindowHideInfo));

		if (!gw->windowHideInfo)
			return;

		gw->windowHideInfo->inputRects = NULL;
		gw->windowHideInfo->nInputRects = 0;
		gw->windowHideInfo->shapeMask = XShapeInputSelected(
				w->screen->display->display, w->id);
		groupClearWindowInputShape(w, gw->windowHideInfo);
 		
 		if (w->frame) {
 			gw->windowHideInfo->frameWindow = w->frame;
 			XUnmapWindow(w->screen->display->display, w->frame);
 		} else
			gw->windowHideInfo->frameWindow = None;
 
 		gw->windowHideInfo->skipState = w->state & 
 					(CompWindowStateSkipPagerMask | CompWindowStateSkipTaskbarMask);

		changeWindowState(w, w->state | CompWindowStateSkipPagerMask |
						  CompWindowStateSkipTaskbarMask);
 	} 
	else if (visible && gw->windowHideInfo) 
	{
 		if (gw->windowHideInfo->nInputRects)
 		{
 			XShapeCombineRectangles(w->screen->display->display, 
 						w->id, ShapeInput, 0, 0, 
 						gw->windowHideInfo->inputRects, 
 						gw->windowHideInfo->nInputRects,
 						ShapeSet, gw->windowHideInfo->inputRectOrdering);
 		} else {
			XShapeCombineMask(w->screen->display->display, w->id,
					ShapeInput, 0, 0, None, ShapeSet);
		}

		if (gw->windowHideInfo->inputRects)
			XFree(gw->windowHideInfo->inputRects);

		XShapeSelectInput(w->screen->display->display, w->id, 
						gw->windowHideInfo->shapeMask);

		if (gw->windowHideInfo->frameWindow) {
			if (w->attrib.map_state != IsUnmapped)
				XMapWindow(w->screen->display->display, w->frame);
		}

		changeWindowState(w, (w->state & ~(CompWindowStateSkipPagerMask |
						  CompWindowStateSkipTaskbarMask)) | gw->windowHideInfo->skipState);

		free(gw->windowHideInfo);
		gw->windowHideInfo = NULL;
	}
}


/*
 * groupTabBarTimeout
 *
 * Description:
 * This function is called when the time expired (== timeout).
 * We use this to realize a delay with the bar hiding after tab change.
 * groupHandleAnimation sets up a timer after the animation has finished.
 * This function itself basically just sets the tab bar to a PaintOff status
 * through calling groupSetTabBarVisibility. The PERMANENT mask allows you to fore
 * a hiding of even PaintPermanentOn tab bars.
 *
 */
static Bool 
groupTabBarTimeout(void *data)
{
	GroupSelection *group = (GroupSelection *) data;

	groupTabSetVisibility(group, FALSE, PERMANENT);

	group->tabBar->timeoutHandle = 0;

	return FALSE;	//This will free the timer.
}

/*
 * groupShowDelayTimeout
 *
 */
static Bool 
groupShowDelayTimeout(void *data)
{
	int mouseX, mouseY;

	GroupSelection *group = (GroupSelection *) data;
	CompScreen *s = group->windows[0]->screen;
	GROUP_SCREEN(s);
	
	if (!HAS_TOP_WIN(group))
	{
		gs->showDelayTimeoutHandle = 0;
		return FALSE;	//This will free the timer.
	}
	
	CompWindow *topTab = group->topTab->window;

	groupGetCurrentMousePosition(s, &mouseX, &mouseY);

	groupRecalcTabBarPos(group, mouseX, WIN_REAL_X(topTab), 
		WIN_REAL_X(topTab) + WIN_REAL_WIDTH(topTab));
	addWindowDamage(topTab);

	groupTabSetVisibility(group, TRUE, 0);
	
	gs->showDelayTimeoutHandle = 0;
	return FALSE;	//This will free the timer.
}

/*
 * groupCheckForVisibleTabBars
 *
 * Description:
 * This function is used to update the gs->tabBarVisible
 * attribute which is used in groupPaintScreen to do add
 * a PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MAS mask to the
 * screen mask. It checks if there are any tab bar's with
 * a PaintState which is no PaintOff on the given screen.
 *
 */
void
groupCheckForVisibleTabBars(CompScreen *s)
{
	GroupSelection *group;
	GROUP_SCREEN(s);

	gs->tabBarVisible = FALSE;

	for (group = gs->groups; group; group = group->next) {
		if (group->tabBar && (group->tabBar->state != PaintOff)) {
			gs->tabBarVisible = TRUE;
			break;
		}
	}
}

/*
 * groupTabSetVisibility
 *
 * Description:
 * This function is used to set the visibility of the tab bar.
 * The "visibility" is indicated through the PaintState, which
 * can be PaintOn, PaintOff, PaintFadeIn, PaintFadeOut
 * and PaintPermantOn.
 * Currently the mask paramater is mostely used for the PERMANENT mask.
 * This mask affects how the visible parameter is handled, for example if
 * visibule is set to TRUE and the mask to PERMANENT state it will set
 * PaintPermanentOn state for the tab bar. When visibile is FALSE, mask 0
 * and the current state of the tab bar is PaintPermanentOn it won't do anything
 * because its not strong enough to disable a Permanent-State, for those you need
 * the mask.
 *
 */
void groupTabSetVisibility(GroupSelection *group, Bool visible, unsigned int mask)
{
	// fixes bad crash...
	if (!group || !group->windows || !group->tabBar || !HAS_TOP_WIN(group))
		return;
	
	GroupTabBar *bar = group->tabBar;
	CompWindow *topTab = TOP_TAB(group);
	PaintState oldState;

	oldState = bar->state;

	/* hide tab bars for invisible top windows */
	if ((topTab->state & CompWindowStateHiddenMask) || topTab->invisible)
	{
		bar->state = PaintOff;
		groupSwitchTopTabInput(group, TRUE);
	} 
	
	else if (visible && bar->state != PaintPermanentOn && 
			(mask & PERMANENT))
	{
		bar->state = PaintPermanentOn;
		groupSwitchTopTabInput(group, FALSE);
	
	}
	
	else if (visible && 
		(bar->state == PaintOff || bar->state == PaintFadeOut))
	{
		bar->state = PaintFadeIn;	
		groupSwitchTopTabInput(group, FALSE);
 	}
 	
 	else if (!visible && (bar->state != PaintPermanentOn || (mask & PERMANENT)) &&
 		(bar->state == PaintOn || bar->state == PaintPermanentOn || bar->state == PaintFadeIn))
 	{
		bar->state = PaintFadeOut;
		groupSwitchTopTabInput(group, TRUE);
 	}

	if (bar->state != oldState && bar->state != PaintPermanentOn) // FIXME remove that when we have a new state for PaintPermanentFadeIn
		bar->animationTime = (groupGetFadeTime(group->screen) * 1000) - 
			     bar->animationTime;

	groupCheckForVisibleTabBars(group->screen);
}

/*
 * groupGetDrawOffsetForSlot
 *
 * Description:
 * Its used when the draggedSlot is dragged to another viewport.
 * It calculates a correct offset to the real slot position.
 *
 */
void
groupGetDrawOffsetForSlot(GroupTabBarSlot *slot, int *hoffset, int *voffset)
{
	if (!slot || !slot->window)
		return;

	CompWindow *w = slot->window;

	GROUP_WINDOW(w);
	GROUP_SCREEN(w->screen);

	if (slot != gs->draggedSlot) {
		if (hoffset)
			*hoffset = 0;
		if (voffset)
			*voffset = 0;

		return;
	}

	int vx, vy;
	int oldX = w->serverX;
	int oldY = w->serverY;
	
	if (gw->group) {
		w->serverX = WIN_X(TOP_TAB(gw->group)) + WIN_WIDTH(TOP_TAB(gw->group)) / 2 - WIN_WIDTH(w) / 2;
		w->serverY = WIN_Y(TOP_TAB(gw->group)) + WIN_HEIGHT(TOP_TAB(gw->group)) / 2 - WIN_HEIGHT(w) / 2;
	}

	defaultViewportForWindow(w, &vx, &vy);

	if (hoffset)
		*hoffset = ((w->screen->x - vx) % w->screen->hsize) * w->screen->width;

	if (voffset)
		*voffset = ((w->screen->y - vy) % w->screen->vsize) * w->screen->height;

	w->serverX = oldX;
	w->serverY = oldY;
}

/*
 * groupHandleScreenActions
 *
 * Description:
 * This function handles some actions, which need to be applied
 * on all windows but only once. This is needed for some stuff,
 * which needs to be called on screen initial, but can't be called
 * inside initScreen because it uses a WRAPED function.
 * It's called from groupPreparePaintScreen.
 *
 */
void groupHandleScreenActions(CompScreen *s)
{
	GROUP_SCREEN(s);

	if (!gs->screenActions)
		return;

	CompWindow *w;
	// we need to do it from top to buttom of the stack to avoid problems
	// with a reload of beryl and tabbed static groups. (topTab will always
	// be above the other windows in the group)
	for(w = s->reverseWindows; w; w = w->prev) {
		GROUP_WINDOW(w);

		if (gs->screenActions & CHECK_WINDOW_PROPERTIES)
		{
			Bool tabbed;
			long int id;
			GLushort color[3];

			/* read window property to see if window was grouped 
			 before - if it was, regroup */
			if (groupCheckWindowProperty(w, &id, &tabbed, color))
			{
				GroupSelection *group = groupFindGroupByID(w->screen, id); 
				groupAddWindowToGroup(w, group, id);
				if (tabbed)
					groupTabGroup(w);

				gw->group->color[0] = color[0];
				gw->group->color[1] = color[1];
				gw->group->color[2] = color[2];
				groupRenderTopTabHighlight(gw->group);
				damageScreen(w->screen);
			}
		}
		
		if (gs->screenActions & APPLY_AUTO_TABBING)
		{
			if (groupGetAutotabCreate(s) && !w->invisible && 
			    matchEval(groupGetWindowMatch(s), w))
			{
				if (!gw->group && (gw->windowState == WindowNormal)) {
					groupAddWindowToGroup(w, NULL, 0);
					groupTabGroup(w);
				}
			}
		}
	}

	// clear up the pending actions
	if (gs->screenActions & CHECK_WINDOW_PROPERTIES)
		gs->screenActions &= ~CHECK_WINDOW_PROPERTIES;
	if (gs->screenActions & APPLY_AUTO_TABBING)
		gs->screenActions &= ~APPLY_AUTO_TABBING;
}

/*
 * groupHandleHoverDetection
 *
 * Description:
 * This function is called from groupPreparePaintScreen to handle
 * the hover detection. This is needed for the text showing up,
 * when you hover a thumb on the thumb bar.
 *
 */
void groupHandleHoverDetection(GroupSelection *group)
{
	GroupTabBar *bar = group->tabBar;

	if (!HAS_TOP_WIN(group))
		return;

	CompWindow *topTab = TOP_TAB(group);

	if (bar->state != PaintOff) {	// Tab-bar is visible.
		int mouseX, mouseY;
		Bool mouseOnScreen;
		
		// first get the current mouse position
		mouseOnScreen = groupGetCurrentMousePosition(group->screen, &mouseX, &mouseY);

		// then check if the mouse is in the last hovered slot -- this saves a lot of CPU usage
		if (mouseOnScreen && !(bar->hoveredSlot && XPointInRegion(bar->hoveredSlot->region, mouseX, mouseY)))
		{
			bar->hoveredSlot = NULL;
		
			Region clip;
			clip = groupGetClippingRegion(topTab);

			GroupTabBarSlot *slot;
			for (slot = bar->slots; slot; slot = slot->next)
			{
				// We need to clip the slot region with the clip region first.
				// This is needed to respect the window stack, so if a window
				// covers a port of that slot, this part won't be used for in-slot-detection.
				Region reg = XCreateRegion();
				XSubtractRegion(slot->region, clip, reg);

				if (XPointInRegion(reg, mouseX, mouseY))
				{
					bar->hoveredSlot = slot;
					break;
				}

				XDestroyRegion(reg);
			}

			XDestroyRegion(clip);
			
			// trigger a FadeOut of the text
			if ((bar->textLayer->state == PaintFadeIn || bar->textLayer->state == PaintOn) &&
			    bar->hoveredSlot != bar->textSlot)
			{
				bar->textLayer->animationTime = (groupGetFadeTextTime(group->screen) * 1000) -
												bar->textLayer->animationTime;
				bar->textLayer->state = PaintFadeOut;
			}
			
			// or trigger a FadeIn of the text
			else if (bar->textLayer->state == PaintFadeOut && bar->hoveredSlot == bar->textSlot && bar->hoveredSlot)
			{
				bar->textLayer->animationTime = (groupGetFadeTextTime(group->screen) * 1000) -
												bar->textLayer->animationTime;
				bar->textLayer->state = PaintFadeIn;
			}
		}
	}
}

/*
 * groupHandleTabBarFade
 *
 * Description:
 * This function is called from groupPreparePaintScreen
 * to handle the tab bar fade. It checks the animationTime and updates it,
 * so we can calculate the alpha of the tab bar in the painting code with it.
 *
 */
void groupHandleTabBarFade(GroupSelection *group, int msSinceLastPaint)
{
	GroupTabBar *bar = group->tabBar;

	if ((bar->state == PaintFadeIn || bar->state == PaintFadeOut) &&
	    bar->animationTime > 0)
	{
		bar->animationTime -= msSinceLastPaint;
		
		if (bar->animationTime < 0)
			bar->animationTime = 0;

		// Fade finished
		if (bar->animationTime == 0)
		{
			if (bar->state == PaintFadeIn) {
				bar->state = PaintOn;
				groupCheckForVisibleTabBars(group->screen);
			}

			else if (bar->state == PaintFadeOut) {
				bar->state = PaintOff;
				groupCheckForVisibleTabBars(group->screen);
		
				if (bar->textLayer)	{
					// Tab-bar is no longer painted, clean up text animation variables.
					bar->textLayer->animationTime = 0;
					bar->textLayer->state = PaintOff;
					bar->textSlot = bar->hoveredSlot = NULL;
						
					groupRenderWindowTitle(group);
				}
			}
		}
	}
}

/*
 * groupHandleTextFade
 *
 * Description:
 * This function is called from groupPreparePaintScreen
 * to handle the text fade. It checks the animationTime and updates it,
 * so we can calculate the alpha of the text in the painting code with it.
 *
 */
void groupHandleTextFade(GroupSelection *group, int msSinceLastPaint)
{
	GroupTabBar *bar = group->tabBar;
	GroupCairoLayer *textLayer = bar->textLayer;		

	if (!textLayer)
		return;

	// Fade in progress...
	if ((textLayer->state == PaintFadeIn || textLayer->state == PaintFadeOut) &&
	    textLayer->animationTime > 0)
	{
		textLayer->animationTime -= msSinceLastPaint;
		
		if (textLayer->animationTime < 0)
			textLayer->animationTime = 0;
		
		// Fade has finished.
		if (textLayer->animationTime == 0) {
			if (textLayer->state == PaintFadeIn)
				textLayer->state = PaintOn;
			
			else if (textLayer->state == PaintFadeOut)
				textLayer->state = PaintOff;
		}
	}
		
	if (textLayer->state == PaintOff && bar->hoveredSlot)
	{
		// Start text animation for the new hovered slot.
		bar->textSlot = bar->hoveredSlot;
		textLayer->state = PaintFadeIn;
		textLayer->animationTime =  (groupGetFadeTextTime(group->screen) * 1000);
			
		groupRenderWindowTitle(group);
	}
		
	else if (textLayer->state == PaintOff && bar->textSlot)
	{
		// Clean Up.
		bar->textSlot = NULL;
		groupRenderWindowTitle(group);
	}
}

/*
 * groupHanldeTabBarAnimation
 *
 * Description: Handles the different animations for the tab bar defined in
 * GroupAnimationType. Basically that means this function updates
 * tabBar->animation->time as well as checking if the animation is already
 * finished.
 *
 */
void groupHandleTabBarAnimation(GroupSelection *group, int msSinceLastPaint)
{
	GroupTabBar *bar = group->tabBar;

	if (bar->bgAnimation)
	{
		bar->bgAnimationTime -= msSinceLastPaint;

		if (bar->bgAnimationTime <= 0) {
			bar->bgAnimationTime = 0;
			bar->bgAnimation = 0;

			groupRenderTabBarBackground(group);
		}
	}
}

/*
 * groupTabChangeActivateEvent
 * 
 * Description: Creates a compiz event to let other plugins know about
 * the starting and ending point of the tab changing animation
 */
static void
groupTabChangeActivateEvent (CompScreen *s,
		     Bool	activating)
{
    CompOption o[2];

    o[0].type = CompOptionTypeInt;
    o[0].name = "root";
    o[0].value.i = s->root;

    o[1].type = CompOptionTypeBool;
    o[1].name = "active";
    o[1].value.b = activating;

    (*s->display->handleCompizEvent) (s->display, "group", "tabChangeActivate", o, 2);
}

/*
 * groupHandleTabChange
 *
 * Description:
 * This function is also called from groupHandleChanges to handle
 * the tab change. It moved the new topTab on the screen as well as doing
 * the initial set for the tab change animation.
 *
 */
static void
groupHandleTabChange(CompScreen *s, GroupSelection *group)
{
	GROUP_SCREEN(s);

	if (!group || !HAS_TOP_WIN(group) || !group->changeTab)
		return;

	// exit when there is a rotate or plane animation
	if (screenGrabExist(s, "rotate", "plane", 0))
		return;

	CompWindow* topTab = TOP_TAB(group);

	if(group->tabbingState != PaintOff) 
	{
		// if the previous top-tab window is being removed from the group, move the new top-tab window onscreen.
		if(group->ungroupState == UngroupSingle && group->prevTopTab == NULL)
		{
			gs->queued = TRUE;
			groupSetWindowVisibility(topTab, TRUE);
			moveWindow(topTab, group->oldTopTabCenterX - WIN_X(topTab) - WIN_WIDTH(topTab) / 2, 
				group->oldTopTabCenterY - WIN_Y(topTab) - WIN_HEIGHT(topTab) / 2, 
				TRUE, TRUE); 
			syncWindowPosition(topTab);
			gs->queued = FALSE;
			
			// recalc here is needed (for y value)!
			groupRecalcTabBarPos(group, (group->tabBar->region->extents.x1 + group->tabBar->region->extents.x2) / 2,
				WIN_REAL_X(topTab), 
				WIN_REAL_X(topTab) + WIN_REAL_WIDTH(topTab));
			
			group->prevTopTab = group->topTab;
		}
		
		return;
	}

	gs->queued = TRUE;
	groupSetWindowVisibility(topTab, TRUE);
	moveWindow(topTab, group->oldTopTabCenterX - WIN_X(topTab) - WIN_WIDTH(topTab) / 2, 
		group->oldTopTabCenterY - WIN_Y(topTab) - WIN_HEIGHT(topTab) / 2, 
		TRUE, TRUE); 
	syncWindowPosition(topTab);
	gs->queued = FALSE;
			
	if(group->prevTopTab) {
		//we use only the half time here - the second half will be PaintFadeOut
		group->changeAnimationTime = groupGetChangeAnimationTime(s) * 500;
		
		groupTabChangeActivateEvent(s, TRUE);

		group->changeState = PaintFadeIn;
				
		group->changeTab = FALSE;
	}
	
	else	//No window to do animation with.
	{
		group->prevTopTab = group->topTab;
		group->changeTab = FALSE;
		activateWindow (TOP_TAB(group));
	}
	
	return;
}

/*
 * groupHandleAnimation
 *
 * Description:
 * This function handles the change animation. It's called
 * from groupHandleChanges. Don't let the changeState
 * confuse you, PaintFadeIn equals with the start of the
 * rotate animation and PaintFadeOut is the end of these
 * animation.
 *
 */
static void
groupHandleAnimation(CompScreen *s, GroupSelection *group)
{
	if(group->tabbingState != PaintOff || !HAS_TOP_WIN(group))
		return;

	if (screenGrabExist(s, "rotate", "plane", 0))
		return;

	if(group->changeState == PaintFadeIn && group->changeAnimationTime <= 0) {

		// recalc here is needed (for y value)!
		groupRecalcTabBarPos(group, (group->tabBar->region->extents.x1 + group->tabBar->region->extents.x2) / 2,
			WIN_REAL_X(TOP_TAB(group)), 
			WIN_REAL_X(TOP_TAB(group)) + WIN_REAL_WIDTH(TOP_TAB(group)));

		group->changeAnimationTime += groupGetChangeAnimationTime(s) * 500;
		
		if (group->changeAnimationTime <= 0)
			group->changeAnimationTime = 0;
		
		group->changeState = PaintFadeOut;
		
		if (HAS_TOP_WIN(group))
			activateWindow (TOP_TAB(group));
	}

	if (group->changeState == PaintFadeOut && group->changeAnimationTime <= 0)
	{
		groupTabChangeActivateEvent(s, FALSE);

		int oldChangeAnimationTime = group->changeAnimationTime;
		
		if (group->prevTopTab)
			groupSetWindowVisibility(PREV_TOP_TAB(group), FALSE);
			
		group->prevTopTab = group->topTab;
		group->changeState = PaintOff;
					
		if (group->nextTopTab) {
			groupChangeTab(group->nextTopTab, group->nextDirection);
			group->nextTopTab = NULL;
						
			groupHandleTabChange(s, group);
			
			if (group->changeState == PaintFadeIn)
				group->changeAnimationTime += oldChangeAnimationTime;	// If a new animation was started.
		}
		
		if (group->changeAnimationTime <= 0)
			group->changeAnimationTime = 0;
		
		else if (groupGetVisibilityTime(s) != 0.0f && group->changeState == PaintOff) {
			groupTabSetVisibility (group, TRUE, PERMANENT | SHOW_BAR_INSTANTLY_MASK);
			
			if (group->tabBar->timeoutHandle)
				compRemoveTimeout(group->tabBar->timeoutHandle);
		
			group->tabBar->timeoutHandle = compAddTimeout (
				groupGetVisibilityTime(s) * 1000, groupTabBarTimeout, group);
		}
	}
}

/*
 * groupHandleTab
 *
 * Description:
 * This functions handes the offscreen moves of the tabs
 * after the animation has finished. It's called from
 * groupHandleChanges.
 *
 */
static void
groupHandleTab(CompScreen *s, GroupSelection *group)
{
	if (group->tabbingState == PaintOff || group->doTabbing ||
	    !HAS_TOP_WIN(group) || !group->changeTab)
		return;
		
	GroupTabBarSlot *slot;
			
	for(slot = group->tabBar->slots; slot; slot = slot->next) {
		if (!slot->window)
			continue;

		CompWindow *w = slot->window;
		GROUP_WINDOW(w);

		if (slot == group->topTab || !(gw->animateState & FINISHED_ANIMATION) || gw->ungroup)
			continue;

		groupSetWindowVisibility(w, FALSE);
	}

	group->changeTab = FALSE;
	group->prevTopTab = group->topTab;
	
	return;
}

/*
 * groupHandleTabbingAnimation
 *
 * Description:
 * This function handles the end of the tab
 * animation. Actually its just sets some
 * states and sync the window positions with X.
 * It's called from groupHandleChanges.
 *
 */
static void
groupHandleTabbingAnimation(CompScreen *s, GroupSelection *group)
{
	int i;

	if (group->tabbingState == PaintOff || group->doTabbing)
		return;
	
	// Not animated any more.
	group->tabbingState = PaintOff;
	groupSyncWindows(group);

	for(i = 0; i < group->nWins; i++) {
		CompWindow *w = group->windows[i];
		GROUP_WINDOW(w);

		gw->animateState = 0;
	}

	return;
}

/*
 * groupHandleUntab
 *
 * Description:
 * This function handles the beginning of the untab
 * animation. It deletes the tab bar and set's
 * group->prevTopTab. It's called from groupHandleChanges.
 *
 */
static void
groupHandleUntab(CompScreen *s, GroupSelection *group)
{
	if (group->tabbingState == PaintOff || !group->doTabbing)
		return;

	if (group->topTab || !group->changeTab)
		return;

	groupDeleteTabBar(group);
				
	group->changeAnimationTime = 0;
	group->changeState = PaintOff;
	group->nextTopTab = NULL;

	group->changeTab = FALSE;
	group->prevTopTab = group->topTab;
	
	return;
}

/*
 * groupHandleUngroup
 *
 * Description:
 * This function handles the ungroup animation for tabbed groups.
 * It moved the windows on screen and also it calles groupDeleteGroupWindow
 * when its a "single ungroup", which means only one window gets removed
 * from the group. Another task of this function is to check if the group
 * which is before the given group in the linked list needs to be deleted.
 * This is needed to avoid problems with the linked list.
 * It's called from groupHandleChanges.
 *
 */
static Bool
groupHandleUngroup(CompScreen *s, GroupSelection *group)
{
	int i;
	GROUP_SCREEN(s);

	if((group->ungroupState == UngroupSingle) && group->doTabbing && group->changeTab)
	{
		for(i = 0; i < group->nWins; i++) {
			CompWindow *w = group->windows[i];
			GROUP_WINDOW(w);
			
			if (gw->ungroup)
			{
				gs->queued = TRUE;
				groupSetWindowVisibility(w, TRUE);
				moveWindow(w, group->oldTopTabCenterX - WIN_X(w) - WIN_WIDTH(w) / 2, 
					group->oldTopTabCenterY - WIN_Y(w) - WIN_HEIGHT(w) / 2, 
					TRUE, TRUE);
				syncWindowPosition(w);
				gs->queued = FALSE;
			}
		}
		
		group->changeTab = FALSE;
		
	}
	
	if ((group->ungroupState == UngroupSingle) && !group->doTabbing) {
		Bool morePending;

		do {
			morePending = FALSE;

			for(i = 0;i < group->nWins; i++) {
				CompWindow *w = group->windows[i];
				GROUP_WINDOW(w);
			
				if (gw->ungroup) {
					groupDeleteGroupWindow(w, TRUE);
					gw->ungroup = FALSE;
					morePending = TRUE;
				}
			}
		} while (morePending);

		group->ungroupState = UngroupNone;
	}

	if (group->prev)
	{
		if ((group->prev->ungroupState == UngroupAll) && !group->prev->doTabbing)
			groupDeleteGroup(group->prev);
	}
	if (!group->next) {
		if ((group->ungroupState == UngroupAll) && !group->doTabbing) {
			groupDeleteGroup(group);
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * groupHandleChanges
 *
 * Description:
 * This function is called from groupPreparePaintScreen to
 * go through all groups and apply the other "handle functions"
 * on them.
 *
 */
void
groupHandleChanges(CompScreen* s)
{
	GROUP_SCREEN(s);

	GroupSelection *group;

	for(group = gs->groups; group; group = group ? group->next : NULL)
	{
		groupHandleUntab(s, group);
		groupHandleTab(s, group);
		groupHandleTabbingAnimation(s, group);
		groupHandleTabChange(s, group);
		groupHandleAnimation(s, group);

		if (!groupHandleUngroup(s, group))
			group = NULL;
	}
}

/* adjust velocity for each animation step (adapted from the scale plugin) */
static int 
adjustTabVelocity(CompWindow * w)
{
	float dx, dy, adjust, amount;
	float x1, y1;

	GROUP_WINDOW(w);

	x1 = y1 = 0.0;

	if (!(gw->animateState & IS_ANIMATED))
		return 0;

	x1 = gw->destination.x;
	y1 = gw->destination.y;

	dx = x1 - (w->serverX + gw->tx);

	adjust = dx * 0.15f;
	amount = fabs(dx) * 1.5f;
	if (amount < 0.5f)
		amount = 0.5f;
	else if (amount > 5.0f)
		amount = 5.0f;

	gw->xVelocity = (amount * gw->xVelocity + adjust) / (amount + 1.0f);

	dy = y1 - (w->serverY + gw->ty);

	adjust = dy * 0.15f;
	amount = fabs(dy) * 1.5f;
	if (amount < 0.5f)
		amount = 0.5f;
	else if (amount > 5.0f)
		amount = 5.0f;

	gw->yVelocity = (amount * gw->yVelocity + adjust) / (amount + 1.0f);

	if (fabs(dx) < 0.1f && fabs(gw->xVelocity) < 0.2f &&
		fabs(dy) < 0.1f && fabs(gw->yVelocity) < 0.2f)
	{
		gw->xVelocity = gw->yVelocity = 0.0f;
		gw->tx = x1 - w->serverX;
		gw->ty = y1 - w->serverY;

		return 0;
	}
	return 1;
}

/*
 * groupDrawTabAnimation
 *
 * Description:
 * This function is called from groupPreparePaintScreen, to move
 * all the animated windows, with the required animation step.
 * The function goes through all grouped animated windows, calculates
 * the required step using adjustTabVelocity, moves the window,
 * and then checks if the animation is finished for that window.
 *
 */
void groupDrawTabAnimation(CompScreen * s, int msSinceLastPaint)
{
	GROUP_SCREEN(s);

	int i;
	
	GroupSelection *group;
	for(group = gs->groups; group; group = group->next)
	{
		if(group->tabbingState == PaintOff)
			continue;

		int steps, dx, dy;
		float amount, chunk;

		amount = msSinceLastPaint * 0.05f * groupGetTabbingSpeed(s);
		steps = amount / (0.5f * groupGetTabbingTimestep(s)); 
		if (!steps)
			steps = 1;
		chunk = amount / (float)steps;

		while (steps--) {
			group->doTabbing = FALSE;

			for(i = 0; i < group->nWins; i++) {
				CompWindow *cw = group->windows[i];

				if(!cw)
					continue;

				GROUP_WINDOW(cw);

				if (!(gw->animateState & IS_ANIMATED))
					continue;

				if (!adjustTabVelocity(cw))
				{
					gw->animateState |= FINISHED_ANIMATION;
					gw->animateState &= ~(IS_ANIMATED);
				}

				gw->tx += gw->xVelocity * chunk;
				gw->ty += gw->yVelocity * chunk;

				dx = (cw->serverX + gw->tx) - cw->attrib.x;
				dy = (cw->serverY + gw->ty) - cw->attrib.y;

				group->doTabbing |= (gw->animateState & IS_ANIMATED);

				gs->queued = TRUE;
				moveWindow(cw, dx, dy, FALSE, FALSE);
				gs->queued = FALSE;
			}
			if (!group->doTabbing)
				break;
		}
	}
}

/*
 * groupUpdateTabBars
 *
 * Description:
 * This function is responsible for showing / unshowing the tab-bars,
 * when the title-bars / tab-bars are hovered.
 * The function is called whenever a new window is entered,
 * checks if the entered window is a window frame (and if the title 
 * bar part of that frame was hovered) or if it was the input
 * prevention window of a tab bar, and sets tab-bar visibility 
 * according to that.
 *
 */
void groupUpdateTabBars(CompScreen *s, Window enteredWin)
{
	GROUP_SCREEN(s);
	CompWindow *w;
	GroupSelection *hoveredGroup = NULL;
	int mouseX = -1, mouseY;

	/* first check if the entered window is a frame */
	for (w = s->windows; w; w = w->next)
	{
		if (w->frame == enteredWin)
			break;
	}

	if (w)
	{
		/* is the window the entered frame belongs to inside 
		   a tabbed group? if no, it's not interesting for us */
		GROUP_WINDOW(w);
		if (gw->group && gw->group->tabBar)
		{
			/* it is grouped and tabbed, so now we have to 
			   check if we hovered the title bar or the frame */
			if (groupGetCurrentMousePosition(s, &mouseX, &mouseY))
			{
				Region reg = XCreateRegion();
				XRectangle rect;
				rect.x = WIN_X(w) - w->input.left;
				rect.y = WIN_Y(w) - w->input.top;
				rect.width = WIN_WIDTH(w) + w->input.right;
				rect.height = WIN_Y(w) - rect.y;
				XUnionRectWithRegion(&rect, reg, reg);

				if (XPointInRegion(reg, mouseX, mouseY))
					hoveredGroup = gw->group;

				XDestroyRegion(reg);
			}
		}
	}

	/* if we didn't hover a title bar, check if we hovered
	   a tab bar (means: input prevention window) */
	if (!hoveredGroup)
	{
		GroupSelection *group;
		for (group = gs->groups; group; group = group->next)
		{
			if (group->inputPrevention == enteredWin)
				hoveredGroup = group;
		}
	}

	/* if we found a hovered a tab bar different than the last one (or
	   left a tab bar), hide the old one */
	if (gs->lastHoveredGroup && (hoveredGroup != gs->lastHoveredGroup))
		groupTabSetVisibility (gs->lastHoveredGroup, FALSE, 0);

	/* if we entered a tab bar (or title bar), show the tab bar */
	if (hoveredGroup && HAS_TOP_WIN(hoveredGroup) && !TOP_TAB(hoveredGroup)->grabbed) 
	{
		GroupTabBar *bar = hoveredGroup->tabBar;

		if (bar && ((bar->state == PaintOff) || (bar->state == PaintFadeOut))) {			
			int showDelayTime = groupGetTabbarShowDelay(s) * 1000;
			
			if (showDelayTime > 0 && bar->state == PaintOff)	// Show the tab-bar after a delay, only if the tab-bar wasn't fading out.
				gs->showDelayTimeoutHandle = 
					compAddTimeout(showDelayTime, groupShowDelayTimeout, hoveredGroup);
			else
				groupShowDelayTimeout(hoveredGroup);
		}
	}

	gs->lastHoveredGroup = hoveredGroup;
}

/*
 * groupGetConstrainRegion
 *
 */
static Region groupGetConstrainRegion(CompScreen *s)
{
	CompWindow *w;
	Region     region;
	REGION     r;
	int        i;
	       
	region = XCreateRegion ();
	if (!region)
		return NULL;

	for (i = 0;i < s->nOutputDev; i++)
		XUnionRegion (&s->outputDev[i].region, region, region);
 	
	r.rects    = &r.extents;
	r.numRects = r.size = 1;
	
	for (w = s->windows; w; w = w->next) {
		if (!w->mapNum)
			 continue;

		if (w->struts) {
			r.extents.x1 = w->struts->top.x;
			r.extents.y1 = w->struts->top.y;
		       	r.extents.x2 = r.extents.x1 + w->struts->top.width;
			r.extents.y2 = r.extents.y1 + w->struts->top.height;
			
			XSubtractRegion (region, &r, region);

			r.extents.x1 = w->struts->bottom.x;
			r.extents.y1 = w->struts->bottom.y;
			r.extents.x2 = r.extents.x1 + w->struts->bottom.width;
			r.extents.y2 = r.extents.y1 + w->struts->bottom.height;
		       
			XSubtractRegion (region, &r, region);

			r.extents.x1 = w->struts->left.x;
			r.extents.y1 = w->struts->left.y;
			r.extents.x2 = r.extents.x1 + w->struts->left.width;
			r.extents.y2 = r.extents.y1 + w->struts->left.height;
		       
			XSubtractRegion (region, &r, region);

			r.extents.x1 = w->struts->right.x;
			r.extents.y1 = w->struts->right.y;
			r.extents.x2 = r.extents.x1 + w->struts->right.width;
			r.extents.y2 = r.extents.y1 + w->struts->right.height;
		       
			XSubtractRegion (region, &r, region);
		}
	}

	return region;
}

/*
 * groupConstrainMovement
 *
 */
static Bool groupConstrainMovement(CompWindow *w, Region constrainRegion, int dx, int dy, 
	int *new_dx, int *new_dy)
{
	GROUP_WINDOW(w);
	int status;
	int origDx = dx, origDy = dy;
	int x, y, width, height;

	if (!gw->group)
		return FALSE;

	if (!dx && !dy)
		return FALSE;

	x = gw->orgPos.x - w->input.left + dx;
	y = gw->orgPos.y - w->input.top + dy;
	width = WIN_REAL_WIDTH(w);
	height = WIN_REAL_HEIGHT(w);

	status = XRectInRegion(constrainRegion, x, y, width, height);

	int xStatus = status;
	while (dx && (xStatus != RectangleIn)) {
		xStatus = XRectInRegion(constrainRegion, x, y - dy,
					width, height);
			
		if (xStatus != RectangleIn)
			dx += (dx < 0) ? 1 : -1;

		x = gw->orgPos.x - w->input.left + dx;
	}

	while (dy && (status != RectangleIn)) {
		status = XRectInRegion(constrainRegion, x, y, 
				width, height);

		if (status != RectangleIn)
			dy += (dy < 0) ? 1 : -1;

		y = gw->orgPos.y - w->input.top + dy;
	}

	if (new_dx)
		*new_dx = dx;

	if (new_dy)
		*new_dy = dy;

	if ((dx != origDx) || (dy != origDy))
		return TRUE;
	else
		return FALSE;
}

/*
 * groupApplyConstrainingToWindows
 *
 */
static void groupApplyConstrainingToWindows(GroupSelection *group, 
	Region constrainRegion, Window constrainedWindow, int dx, int dy)
{
	int i;
	CompWindow *w;

	if (!dx && !dy)
		return;

	for (i = 0; i < group->nWins; i++) {
		w = group->windows[i];
		GROUP_WINDOW(w);

		/* ignore certain windows: we don't want to apply
		   the constraining results on the constrained window
		   itself, not do we want to change the target position
		   of unamimated windows and of windows which
		   already are constrained */
		if (w->id == constrainedWindow)
			continue;

		if(!(gw->animateState & IS_ANIMATED))
			continue;

		if (gw->animateState & DONT_CONSTRAIN)
			continue;

		if (!(gw->animateState & CONSTRAINED_X)) {
			gw->animateState |= IS_ANIMATED;

			/* applying the constraining result of another window
			   might move the window offscreen, too, so check
			   if this is not the case */
			if (groupConstrainMovement(w, constrainRegion, dx, 0, &dx, NULL))
				gw->animateState |= CONSTRAINED_X;

			gw->destination.x += dx;
			gw->orgPos.x += dx;
		}

		if (!(gw->animateState & CONSTRAINED_Y)) {
			gw->animateState |= IS_ANIMATED;

			/* analog to X case */
			if (groupConstrainMovement(w, constrainRegion, 0, dy, NULL, &dy))
				gw->animateState |= CONSTRAINED_Y;

			gw->destination.y += dy;
			gw->orgPos.y += dy;
		}
	}
}

/*
 * groupStartTabbingAnimation
 *
 */
void groupStartTabbingAnimation(GroupSelection *group, Bool tab)
{
	if (!group || (group->tabbingState != PaintOff))
		return;

	CompScreen *s = group->windows[0]->screen;
	int i;
	int dx, dy;
	int constrainStatus;

	group->doTabbing = TRUE;
	group->changeTab = TRUE;

	group->tabbingState = (tab) ? PaintFadeIn : PaintFadeOut;

	if (!tab) {
		/* we need to set up the X/Y constraining on untabbing */
		Region constrainRegion = groupGetConstrainRegion(s);
		Bool constrainedWindows = TRUE;

		if (!constrainRegion)
			return;

		/* reset all flags */
		for (i = 0; i < group->nWins; i++) {
			GROUP_WINDOW(group->windows[i]);
			gw->animateState &= ~(CONSTRAINED_X | CONSTRAINED_Y | DONT_CONSTRAIN);
		}

		/* as we apply the constraining in a flat loop,
		   we may need to run multiple times through this
		   loop until all constraining dependencies are met */
		while (constrainedWindows) {
			constrainedWindows = FALSE;
			/* loop through all windows and try to constrain their
		   	   animation path (going from gw->orgPos to 
		   	   gw->destination) to the active screen area */
			for (i = 0; i < group->nWins; i++) {
				CompWindow *w = group->windows[i];
				GROUP_WINDOW(w);

				/* ignore windows which aren't animated and/or
				   already are at the edge of the screen area */
				if (!(gw->animateState & IS_ANIMATED))
					continue;

				if (gw->animateState & DONT_CONSTRAIN)
					continue;

				/* is the original position inside the screen area? */
				constrainStatus = XRectInRegion(constrainRegion,
						gw->orgPos.x  - w->input.left, 
						gw->orgPos.y - w->input.top,
						WIN_REAL_WIDTH(w), WIN_REAL_HEIGHT(w));

				/* constrain the movement */
				if (groupConstrainMovement(w, constrainRegion,
					    gw->destination.x - gw->orgPos.x,
					    gw->destination.y - gw->orgPos.y, &dx, &dy)) {
					/* handle the case where the window is outside the screen
					   area on its whole animation path */
					if (constrainStatus != RectangleIn && !dx && !dy) {
						gw->animateState |= DONT_CONSTRAIN;
						gw->animateState |= CONSTRAINED_X | CONSTRAINED_Y;

						/* use the original position as last resort */
						gw->destination.x = gw->mainTabOffset.x;
						gw->destination.y = gw->mainTabOffset.y;
					} else {
						/* if we found a valid target position, apply
						   the change also to other windows to retain
						   the distance between the windows */
						groupApplyConstrainingToWindows(group, 
							constrainRegion, w->id, 
							dx - gw->destination.x + gw->orgPos.x, 
							dy - gw->destination.y + gw->orgPos.y);

						/* if we hit constraints, adjust the mask and the 
						   target position accordingly */
		    				if (dx != (gw->destination.x - gw->orgPos.x)) {
							gw->animateState |= CONSTRAINED_X;
							gw->destination.x = gw->orgPos.x + dx;
						}
						
						if (dy != (gw->destination.y - gw->orgPos.y)) {
							gw->animateState |= CONSTRAINED_Y;
							gw->destination.y = gw->orgPos.y + dy;
						}

						constrainedWindows = TRUE;
					}
				}
			}
		}
		XDestroyRegion(constrainRegion);
	}
}

/*
 * groupTabGroup
 *
 */
void groupTabGroup(CompWindow *main)
{
	GROUP_WINDOW(main);
	GroupSelection *group = gw->group;

	if(!group || group->tabBar)
		return;

	if (!main->screen->display->shapeExtension) {
		printf("group plugin: No X shape extension! Tabbing won't work...\n");
		return;
	}

	groupInitTabBar(group, main);
	groupCreateInputPreventionWindow(group);
	
	group->tabbingState = PaintOff;
	groupChangeTab(gw->slot, RotateUncertain);	//Slot is initialized after groupInitTabBar(group);
	groupRecalcTabBarPos(gw->group, WIN_X(main) + WIN_WIDTH(main)/2,   
 			WIN_X(main), WIN_X(main) + WIN_WIDTH(main)); 
	
	int width, height;
	width = group->tabBar->region->extents.x2 - group->tabBar->region->extents.x1;
	height = group->tabBar->region->extents.y2 - group->tabBar->region->extents.y1;
	
	group->tabBar->textLayer = groupCreateCairoLayer(main->screen, width, height);
	group->tabBar->textLayer->state = PaintOff;
	group->tabBar->textLayer->animationTime = 0;
	groupRenderWindowTitle(group);
	group->tabBar->textLayer->animationTime = groupGetFadeTextTime(main->screen) * 1000;
	group->tabBar->textLayer->state = PaintFadeIn;
	
	// we need a buffer for DnD here
	int space = groupGetThumbSpace(main->screen);
	int thumb_size = groupGetThumbSize(main->screen);
	group->tabBar->bgLayer = groupCreateCairoLayer(main->screen, width + space + thumb_size, height);
	group->tabBar->bgLayer->state = PaintOn;
	group->tabBar->bgLayer->animationTime = 0;
	groupRenderTabBarBackground(group);

	width = group->topTab->region->extents.x2 - group->topTab->region->extents.x1;
	height = group->topTab->region->extents.y2 - group->topTab->region->extents.y1;

	group->tabBar->selectionLayer = groupCreateCairoLayer(main->screen, width, height);
	group->tabBar->selectionLayer->state = PaintOn;
	group->tabBar->selectionLayer->animationTime = 0;
	groupRenderTopTabHighlight(group);

	if(!HAS_TOP_WIN(group))
		return;

	GroupTabBarSlot *slot;
					
	for(slot = group->tabBar->slots; slot; slot = slot->next) {		
		CompWindow *cw = slot->window;

		GROUP_WINDOW(cw);
		
		int x = WIN_X(cw), y = WIN_Y(cw);

		if(gw->animateState & IS_ANIMATED)
		{
			x = gw->destination.x;
			y = gw->destination.y;
		}
		
		// center the window to the main window
		gw->destination.x = WIN_X(main) + (WIN_WIDTH(main) / 2) - (WIN_WIDTH(cw) / 2);
		gw->destination.y = WIN_Y(main) + (WIN_HEIGHT(main) / 2) - (WIN_HEIGHT(cw) / 2);

		gw->mainTabOffset.x = x - gw->destination.x;	//Distance from destination.
		gw->mainTabOffset.y = y - gw->destination.y;

		gw->orgPos.x = WIN_X(cw);
		gw->orgPos.y = WIN_Y(cw);

		gw->tx = gw->ty = gw->xVelocity = gw->yVelocity = 0.0f;

		gw->animateState |= IS_ANIMATED;
	}

	groupStartTabbingAnimation(group, TRUE);
}

/*
 * groupUntabGroup
 *
 */
void
groupUntabGroup(GroupSelection *group)
{
	if(!HAS_TOP_WIN(group))
		return;

	GROUP_WINDOW(TOP_TAB(group));
	GROUP_SCREEN(TOP_TAB(group)->screen);	
	
	int mainOrgPosX = gw->mainTabOffset.x;
	int mainOrgPosY = gw->mainTabOffset.y;
	int oldX, oldY;
	
	CompWindow* prevTopTab;
	
	if(group->prevTopTab)
		prevTopTab = PREV_TOP_TAB(group);
	else
		prevTopTab = TOP_TAB(group);	//If prevTopTab isn't set, we have no choice but using topTab.
						//It happens when there is still animation, which means the tab wasn't changed anyway.
	
	group->oldTopTabCenterX = WIN_X(prevTopTab) + WIN_WIDTH(prevTopTab)/2;
	group->oldTopTabCenterY = WIN_Y(prevTopTab) + WIN_HEIGHT(prevTopTab)/2;
	
	group->topTab = NULL;

	GroupTabBarSlot *slot;
		
	for(slot = group->tabBar->slots; slot; slot = slot->next) {

		CompWindow *cw = slot->window;

		GROUP_WINDOW(cw);

		gs->queued = TRUE;
		groupSetWindowVisibility(cw, TRUE);
		moveWindow(cw, group->oldTopTabCenterX - WIN_X(cw) - WIN_WIDTH(cw) / 2, 
			group->oldTopTabCenterY - WIN_Y(cw) - WIN_HEIGHT(cw) / 2, 
			TRUE, TRUE);
		syncWindowPosition(cw);
		gs->queued = FALSE;

		/* save the old original position - we might need it if constraining fails */
		oldX = gw->orgPos.x;
		oldY = gw->orgPos.y;

		gw->orgPos.x = group->oldTopTabCenterX - WIN_WIDTH(cw) / 2;
		gw->orgPos.y = group->oldTopTabCenterY - WIN_HEIGHT(cw) / 2;

		gw->destination.x = WIN_X(prevTopTab) + WIN_WIDTH(prevTopTab)/2 - WIN_WIDTH(cw)/2 + gw->mainTabOffset.x - mainOrgPosX;
		gw->destination.y = WIN_Y(prevTopTab) + WIN_HEIGHT(prevTopTab)/2 - WIN_HEIGHT(cw)/2 + gw->mainTabOffset.y - mainOrgPosY;

		gw->mainTabOffset.x = oldX;
		gw->mainTabOffset.y = oldY;

		gw->animateState |= IS_ANIMATED;
		gw->tx = gw->ty = gw->xVelocity = gw->yVelocity = 0.0f;
	}

	group->tabbingState = PaintOff;
	groupStartTabbingAnimation(group, FALSE);

	damageScreen(group->screen);
}

/*
 * groupChangeTab
 *
 */
Bool
groupChangeTab(GroupTabBarSlot* topTab, ChangeTabAnimationDirection direction)
{
	if (!topTab)
		return TRUE;

	CompWindow* w = topTab->window;
	GROUP_WINDOW(w);
	
	GroupSelection *group = gw->group;

	if (!group || group->tabbingState != PaintOff)
		return TRUE;
	
	if (group->changeState == PaintOff && group->topTab == topTab)
		return TRUE;
	
	if (group->changeState != PaintOff && group->nextTopTab == topTab)
		return TRUE;

	if (group->prevTopTab && group->changeState == PaintOff) {
		group->oldTopTabCenterX = WIN_X(PREV_TOP_TAB(group)) + WIN_WIDTH(PREV_TOP_TAB(group))/2;
		group->oldTopTabCenterY = WIN_Y(PREV_TOP_TAB(group)) + WIN_HEIGHT(PREV_TOP_TAB(group))/2;
	}
	
	if (group->changeState != PaintOff)
		group->nextDirection = direction;
	else if (direction == RotateLeft) 
		group->changeAnimationDirection = 1;
	else if (direction == RotateRight)
		group->changeAnimationDirection = -1;
	else {
		int distanceOld = 0, distanceNew = 0;
		GroupTabBarSlot *slot;

		if (group->topTab)
			for (slot = group->tabBar->slots; slot && (slot != group->topTab); 
				slot = slot->next, distanceOld++);

		for (slot = group->tabBar->slots; slot && (slot != topTab); 
			slot = slot->next, distanceNew++);

		if (distanceNew < distanceOld)
			group->changeAnimationDirection = 1;   //left
		else 
			group->changeAnimationDirection = -1;    //right

		//check if the opposite direction is shorter
		if (abs(distanceNew - distanceOld) > (group->tabBar->nSlots / 2))
			group->changeAnimationDirection *= -1;
	}

	if (group->changeState != PaintOff)
	{
		if (group->prevTopTab == topTab)
		{
			// Reverse animation.
			GroupTabBarSlot *tmp = group->topTab;
			group->topTab = group->prevTopTab;
			group->prevTopTab = tmp;
			
			group->changeAnimationDirection *= -1;
			group->changeAnimationTime = groupGetChangeAnimationTime(w->screen) * 500 - group->changeAnimationTime;
			group->changeState = (group->changeState == PaintFadeIn)? PaintFadeOut: PaintFadeIn;
			
			group->nextTopTab = NULL;
		}
		
		else
			group->nextTopTab = topTab;
	}
	
	else
	{
		group->topTab = topTab;
		group->changeTab = (group->prevTopTab != topTab);

		groupRenderWindowTitle(group);
		groupRenderTopTabHighlight(group);
		addWindowDamage(w);
	}

	return TRUE;
}

/*
 * groupRebuildCairoLayer
 *
 */
GroupCairoLayer* groupRebuildCairoLayer(CompScreen *s, GroupCairoLayer *layer, int width, int height)
{
	int timeBuf = layer->animationTime;
	PaintState stateBuf = layer->state;

	groupDestroyCairoLayer(s, layer);
	layer = groupCreateCairoLayer(s, width, height);

	layer->animationTime = timeBuf;
	layer->state = stateBuf;

	return layer;
}

/*
 * groupClearCairoLayer
 *
 */
void groupClearCairoLayer(GroupCairoLayer *layer)
{
	cairo_t *cr = layer->cairo;
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);
}

/*
 * groupDestroyCairoLayer
 *
 */
void groupDestroyCairoLayer(CompScreen *s, GroupCairoLayer *layer)
{
	if (layer->cairo)
		cairo_destroy(layer->cairo);

	if (layer->surface)
		cairo_surface_destroy(layer->surface);;

	if (&layer->texture)
		finiTexture(s, &layer->texture);

	if (layer->pixmap)
		XFreePixmap(s->display->display, layer->pixmap);

	free(layer);
}

/*
 * groupCreateCairoLayer
 *
 */
GroupCairoLayer* groupCreateCairoLayer(CompScreen *s, int width, int height)
{
	GroupCairoLayer* layer = (GroupCairoLayer*) malloc(sizeof(GroupCairoLayer));
	
	Screen *screen;
	Display *display;
	display = s->display->display;
	screen = ScreenOfDisplay (display, s->screenNum);

	layer->surface	= NULL;
	layer->cairo	= NULL;
	layer->animationTime = 0;
	layer->state = PaintOff;
	layer->texWidth = width;
	layer->texHeight = height;
	layer->pixmap = None;

	initTexture(s, &layer->texture);

	XRenderPictFormat *format;
	format = XRenderFindStandardFormat (display, PictStandardARGB32);
	layer->pixmap = XCreatePixmap (display, s->root, width, height, 32);

	if (!bindPixmapToTexture(s, &layer->texture, layer->pixmap, width, height, 32)) {
		XFreePixmap(display, layer->pixmap);
		free(layer);
		return NULL;
	}

	layer->surface = 
		cairo_xlib_surface_create_with_xrender_format(display, layer->pixmap, screen, format, width, height);

	if (cairo_surface_status(layer->surface) != CAIRO_STATUS_SUCCESS) {
		XFreePixmap(display, layer->pixmap);
		free(layer);
        return NULL;
	}

	layer->cairo = cairo_create(layer->surface);
	if (cairo_status(layer->cairo) != CAIRO_STATUS_SUCCESS) {
		XFreePixmap(display, layer->pixmap);
		free(layer);
        return NULL;
	}

	groupClearCairoLayer(layer);

	return layer;
}

/*
 * groupRecalcSlotPos
 *
 */
static void 
groupRecalcSlotPos(GroupTabBarSlot *slot, int slotPos)
{
	GROUP_WINDOW (slot->window);
	GroupSelection *group = gw->group;
	XRectangle box;

	if (!HAS_TOP_WIN(group) || !group->tabBar)
		return;

	int space = groupGetThumbSpace(slot->window->screen);
	int thumb_size = groupGetThumbSize(slot->window->screen);

	EMPTY_REGION(slot->region);

	box.x = space + ((thumb_size + space) * slotPos);
	box.y = space;

	box.width = thumb_size;
	box.height = thumb_size;

	XUnionRectWithRegion(&box, slot->region, slot->region);
}

/*
 * groupRecalcTabBarPos
 *
 */
void groupRecalcTabBarPos(GroupSelection *group, int middleX, int minX1, int maxX2)
{
	if (!HAS_TOP_WIN(group) || !group->tabBar)
		return;

	GroupTabBarSlot *slot;
	GroupTabBar *bar = group->tabBar;
	CompWindow *topTab = TOP_TAB(group);
	Bool isDraggedSlotGroup = FALSE;
	GROUP_SCREEN(group->screen);

	/* first damage the old region to make sure it is updated */
	damageScreenRegion (group->screen, bar->region);

	int space = groupGetThumbSpace(group->screen);
	int old_width = bar->region->extents.x2 - bar->region->extents.x1; 
	int bar_width;
	int currentSlot;
	XRectangle box;

	// calculate the space which the tabs need
	int tabs_width = 0;
	int tabs_height = 0;
	for(slot = bar->slots; slot; slot = slot->next)
	{
		if (slot == gs->draggedSlot && gs->dragged) {
			isDraggedSlotGroup = TRUE;
			continue;
		}
	
		tabs_width += (slot->region->extents.x2 - slot->region->extents.x1);
		if ((slot->region->extents.y2 - slot->region->extents.y1) > tabs_height)
			tabs_height = slot->region->extents.y2 - slot->region->extents.y1;
	}

	// just a little work-a-round for first call
	int thumb_size = groupGetThumbSize(group->screen);
	if (bar->nSlots && tabs_width <= 0)	
	{ 
		// first call
		tabs_width = thumb_size * bar->nSlots;
		
		if (bar->nSlots && tabs_height < thumb_size) // we need to do the standard height too
			tabs_height = thumb_size;
		
		if(isDraggedSlotGroup)
			tabs_width -= thumb_size;
	}

	bar_width = space * (bar->nSlots + 1) + tabs_width;

	if (isDraggedSlotGroup)
		bar_width -= space;  //1 tab is missing, so we have 1 less border

	if (maxX2 - minX1 < bar_width) 
		box.x = (maxX2 + minX1)/2 - bar_width / 2;	
	else if (middleX - bar_width/2 < minX1)
		box.x = minX1;	
	else if (middleX + bar_width/2 > maxX2)
		box.x = maxX2 - bar_width;	
	else
		box.x = middleX - bar_width / 2;

	box.y = WIN_Y(topTab);
	box.width = bar_width;
	box.height = space * 2 + tabs_height;

	EMPTY_REGION(bar->region);
	XUnionRectWithRegion(&box, bar->region, bar->region);
	
	// recalc every slot region
	currentSlot = 0;
	for(slot = bar->slots; slot; slot = slot->next)
	{
		if(slot == gs->draggedSlot && gs->dragged)
			continue;
	
		groupRecalcSlotPos (slot, currentSlot);
		XOffsetRegion (slot->region, 
			bar->region->extents.x1,
			bar->region->extents.y1);
		slot->springX = (slot->region->extents.x1 + slot->region->extents.x2) / 2;
		slot->speed = 0;
		slot->msSinceLastMove = 0;
		
		currentSlot++;
	}
	
	bar->leftSpringX = box.x;
	bar->rightSpringX = box.x + box.width;
	
	bar->rightSpeed = 0;
	bar->leftSpeed = 0;
	
	bar->rightMsSinceLastMove = 0;
	bar->leftMsSinceLastMove = 0;
	
	groupUpdateInputPreventionWindow(group);

	// size changed rebuild TabBar
	if (box.width != old_width && bar->bgLayer) {
		bar->bgLayer = groupRebuildCairoLayer(group->screen, bar->bgLayer, box.width + space + thumb_size, box.height);
		bar->oldWidth = 0; // trigger repaint
	}
}

/*
 * groupInsertTabBarSlotBefore
 *
 */
void groupInsertTabBarSlotBefore(GroupTabBar *bar, GroupTabBarSlot *slot, GroupTabBarSlot *nextSlot)
{
	GroupTabBarSlot *prev = nextSlot->prev;

	if(prev) {
		slot->prev = prev;
		prev->next = slot;
	} else {
		bar->slots = slot;
		slot->prev = NULL;
	}

	slot->next = nextSlot;
	nextSlot->prev = slot;
	bar->nSlots++;

	CompWindow *w = slot->window;
	GROUP_WINDOW(w);
	// Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
	// because the tab-bar got wider now, so it will put it in the average between them,
	// which is (bar->region->extents.x1 + bar->region->extents.x2) / 2 anyway.
	groupRecalcTabBarPos(gw->group, (bar->region->extents.x1 + bar->region->extents.x2) / 2,
			     bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupInsertTabBarSlotAfter
 *
 */
void groupInsertTabBarSlotAfter(GroupTabBar *bar, GroupTabBarSlot *slot, GroupTabBarSlot *prevSlot)
{
	GroupTabBarSlot *next = prevSlot->next;

	if (next) {
		slot->next = next;
		next->prev = slot;
	} else {
		bar->revSlots = slot;
		slot->next = NULL;
	}

	slot->prev = prevSlot;
	prevSlot->next = slot;
	bar->nSlots++;

	CompWindow *w = slot->window;
	GROUP_WINDOW(w);
	// Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
	// because the tab-bar got wider now, so it will put it in the average between them,
	// which is (bar->region->extents.x1 + bar->region->extents.x2) / 2 anyway.
	groupRecalcTabBarPos(gw->group, (bar->region->extents.x1 + bar->region->extents.x2) / 2,
			     bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupInsertTabBarSlot
 *
 */
void groupInsertTabBarSlot(GroupTabBar *bar, GroupTabBarSlot *slot)
{

	if (bar->slots != NULL) {
			bar->revSlots->next = slot;
			slot->prev = bar->revSlots;
			slot->next = NULL;
	} else {
			slot->prev = NULL;
			slot->next = NULL;
			bar->slots = slot;
	}

	bar->revSlots = slot;
	bar->nSlots++;

	CompWindow *w = slot->window;
	GROUP_WINDOW(w);
	// Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
	// because the tab-bar got wider now, so it will put it in the average between them,
	// which is (bar->region->extents.x1 + bar->region->extents.x2) / 2 anyway.
	groupRecalcTabBarPos(gw->group, (bar->region->extents.x1 + bar->region->extents.x2) / 2,
			     bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupUnhookTabBarSlot
 *
 */
void groupUnhookTabBarSlot(GroupTabBar *bar, GroupTabBarSlot *slot, Bool temporary)
{
	GroupTabBarSlot *prev = slot->prev;
	GroupTabBarSlot *next = slot->next;
	
	if(prev)
		prev->next = next;
	else
		bar->slots = next;
	
	if(next)
		next->prev = prev;
	else
		bar->revSlots = prev;

	slot->prev = NULL;
	slot->next = NULL;

	bar->nSlots--;

	CompWindow *w = slot->window;
	GROUP_WINDOW(w);

	if (IS_TOP_TAB(w, gw->group) && !temporary)
	{
		if (next)
			groupChangeTab(next, RotateRight);
		else if (prev)
			groupChangeTab(prev, RotateLeft);
		else if (gw->group->nWins == 1)
			gw->group->topTab = NULL;

		if (groupGetUntabOnClose(w->screen))
			groupUntabGroup(gw->group);
	}

	if(IS_PREV_TOP_TAB(w, gw->group) && !temporary)
		gw->group->prevTopTab = NULL;

	if (slot == bar->hoveredSlot)
		bar->hoveredSlot = NULL;
	
	if (slot == bar->textSlot)
	{
		bar->textSlot = NULL;
		
		if (bar->textLayer->state == PaintFadeIn || bar->textLayer->state == PaintOn)
		{
			bar->textLayer->animationTime = (groupGetFadeTextTime(w->screen) * 1000) -
											bar->textLayer->animationTime;
			bar->textLayer->state = PaintFadeOut;
		}
	}

	// Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
	// because the tab-bar got thiner now, so (bar->region->extents.x1 + bar->region->extents.x2) / 2
	// Won't cause the new x1 / x2 to be outside the original region.
	groupRecalcTabBarPos(gw->group, (bar->region->extents.x1 + bar->region->extents.x2) / 2,
			     bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupDeleteTabBarSlot
 *
 */
void groupDeleteTabBarSlot(GroupTabBar *bar, GroupTabBarSlot *slot)
{
	groupUnhookTabBarSlot(bar, slot, FALSE);

	if (slot->region)
		XDestroyRegion(slot->region);

	CompWindow *w = slot->window;
	CompScreen *s = w->screen;
	GROUP_WINDOW(w);
	GROUP_SCREEN(s);
		
	if(slot == gs->draggedSlot)
	{
		gs->draggedSlot = NULL;
		gs->dragged = FALSE;

		if (gs->grabState == ScreenGrabTabDrag)
			groupGrabScreen(s, ScreenGrabNone);
	}
	
	gw->slot = NULL;
	groupUpdateWindowProperty(w);
	free(slot);
}

/*
 * groupCreateSlot
 *
 */
void groupCreateSlot(GroupSelection *group, CompWindow *w)
{
	if(!group->tabBar)
		return;

	GROUP_WINDOW(w);

	GroupTabBarSlot *slot = (GroupTabBarSlot*) malloc(sizeof(GroupTabBarSlot));
	slot->window = w;

	slot->region = XCreateRegion();

	groupInsertTabBarSlot(group->tabBar, slot);
	gw->slot = slot;
	groupUpdateWindowProperty(w);
}

#define SPRING_K	 groupGetDragSpringK(s)
#define FRICTION	 groupGetDragFriction(s)
#define SIZE		 groupGetThumbSize(s)
#define BORDER		 groupGetBorderRadius(s)
#define Y_START_MOVE groupGetDragYDistance(s)
#define SPEED_LIMIT	 groupGetDragSpeedLimit(s)

/*
 * groupSpringForce
 *
 */
static int groupSpringForce(CompScreen *s, int centerX, int springX)
{
	// Each slot has a spring attached to it, starting at springX, and ending at the center of the slot (centerX).
	// The spring will cause the slot to move, using the well-known physical formula F = k * dl... 
	
	return -SPRING_K * (centerX - springX);
}

/*
 * groupDraggedSlotForce
 *
 */
static int groupDraggedSlotForce(CompScreen *s, int distanceX, int distanceY)
{
	// The dragged slot will make the slot move, to get DnD animations (slots will make room for the newly inserted slot).
	// As the dragged slot is closer to the slot, it will put more force on the slot, causing it to make room for the dragged slot...
	// But if the dragged slot gets too close to the slot, they are going to be reordered soon, so the force will get lower.
	
	// If the dragged slot is in the other side of the slot, it will have to make force in the opposite direction.
	
	// So we the needed funtion is an odd function that goes up at first, and down after that.
	// Sinus is a function like that... :)
	
	// The maximum is got when x = (x1 + x2) / 2, in this case: x = SIZE + BORDER.
	// Because of that, for x = SIZE + BORDER, we get a force of SPRING_K * (SIZE + BORDER) / 2.
	// That equals to the force we get from the the spring.
	// This way, the slot won't move when its distance from the dragged slot is SIZE + BORDER (which is the default distance between slots).

	float a = SPRING_K * (SIZE + BORDER) / 2;	// The maximum value.
	float b = PI /  (2 * SIZE + 2 * BORDER);	// This will make distanceX == 2 * (SIZE + BORDER) to get 0, 
							// and distanceX == (SIZE + BORDER) to get the maximum.
	
	// If there is some distance between the slots in the y axis, the slot should get less force...
	// For this, we change max to a lower value, using a simple linear function.
	
	if(distanceY < Y_START_MOVE)
		a *= 1.0f - (float)distanceY / Y_START_MOVE;
	else
		a = 0;
	
	if(abs(distanceX) < 2 * (SIZE + BORDER))
		return a * sin(b * distanceX);
	else
		return 0;
}

/*
 * groupApplyFriction
 *
 */
static void groupApplyFriction(CompScreen *s, int *speed)
{
	if(abs(*speed) < FRICTION)
		*speed = 0;
			
	else if(*speed > 0)
		*speed -= FRICTION;
		
	else if(*speed < 0)
		*speed += FRICTION;
}

/*
 * groupApplySpeedLimit
 *
 */
static void groupApplySpeedLimit(CompScreen *s, int *speed)
{
	if(*speed > SPEED_LIMIT)
		*speed = SPEED_LIMIT;
	
	else if(*speed < -SPEED_LIMIT)
		*speed = - SPEED_LIMIT;
}

/*
 * groupApplyForces
 *
 */
void groupApplyForces(CompScreen *s, GroupTabBar *bar, GroupTabBarSlot* draggedSlot)
{
	GroupTabBarSlot* slot, *slot2;
	int centerX, centerY;
	int draggedCenterX, draggedCenterY;

	if (draggedSlot) {
		int vx, vy;
		groupGetDrawOffsetForSlot(draggedSlot, &vx, &vy);

		draggedCenterX = ((draggedSlot->region->extents.x1 + draggedSlot->region->extents.x2) / 2) + vx;
		draggedCenterY = ((draggedSlot->region->extents.y1 + draggedSlot->region->extents.y2) / 2) + vy;
	} else {
		draggedCenterX = 0;
		draggedCenterY = 0;
	}
	
	bar->leftSpeed += groupSpringForce(s, bar->region->extents.x1, bar->leftSpringX);
	bar->rightSpeed += groupSpringForce(s, bar->region->extents.x2, bar->rightSpringX);
	
	if (draggedSlot)
	{
		int leftForce = groupDraggedSlotForce(s, bar->region->extents.x1 - SIZE / 2 - draggedCenterX,
							abs((bar->region->extents.y1 + bar->region->extents.y2) / 2 - draggedCenterY));
		
		int rightForce = groupDraggedSlotForce(s, bar->region->extents.x2 + SIZE / 2 - draggedCenterX,
							abs((bar->region->extents.y1 + bar->region->extents.y2) / 2 - draggedCenterY));
		
		if(leftForce < 0)
			bar->leftSpeed += leftForce;
		if(rightForce > 0)
			bar->rightSpeed += rightForce;
	}
	
	for(slot = bar->slots; slot; slot = slot->next)
	{
		centerX = (slot->region->extents.x1 + slot->region->extents.x2) / 2;
		centerY = (slot->region->extents.y1 + slot->region->extents.y2) / 2;
		
		slot->speed += groupSpringForce(s, centerX, slot->springX);
		
		if(draggedSlot && draggedSlot != slot)
		{		
			int draggedSlotForce = groupDraggedSlotForce(s, centerX - draggedCenterX,
									abs(centerY - draggedCenterY));
			
			slot->speed += draggedSlotForce ;
			
			slot2 = NULL;
			
			if(draggedSlotForce < 0)
			{
				slot2 = slot->prev;
				bar->leftSpeed += draggedSlotForce;
			}
			
			else if(draggedSlotForce > 0)
			{
				slot2 = slot->next;
				bar->rightSpeed += draggedSlotForce;
			}
			
			for( ; slot2; slot2 = (draggedSlotForce < 0)? slot2->prev: slot2->next)
			{
				if(slot2 != draggedSlot)
					slot2->speed += draggedSlotForce;
			}
		}
	}
	
	for(slot = bar->slots; slot; slot = slot->next)
	{
		groupApplyFriction(s, &slot->speed);
		groupApplySpeedLimit(s, &slot->speed);
	}
	
	groupApplyFriction(s, &bar->leftSpeed);
	groupApplySpeedLimit(s, &bar->leftSpeed);
	
	groupApplyFriction(s, &bar->rightSpeed);
	groupApplySpeedLimit(s, &bar->rightSpeed);
}

/*
 * groupApplySpeeds
 *
 */
void groupApplySpeeds(CompScreen* s, GroupTabBar* bar, int msSinceLastRepaint)
{
	GroupTabBarSlot* slot;
	int move;
	XRectangle box;
	Bool updateTabBar = FALSE;
	
	box.x = bar->region->extents.x1;
	box.y = bar->region->extents.y1; 
	box.width = bar->region->extents.x2 - bar->region->extents.x1;
	box.height = bar->region->extents.y2 - bar->region->extents.y1;
	
	bar->leftMsSinceLastMove += msSinceLastRepaint;
	bar->rightMsSinceLastMove += msSinceLastRepaint;
	
	// Left
	move = bar->leftSpeed * bar->leftMsSinceLastMove / 1000;
	
	if(move)
	{
		box.x += move;
		box.width -= move;

		bar->leftMsSinceLastMove = 0;
		updateTabBar = TRUE;
	}
	
	else if(bar->leftSpeed == 0 && bar->region->extents.x1 != bar->leftSpringX && 
		SPRING_K * abs(bar->region->extents.x1 - bar->leftSpringX) < FRICTION)
	{
		// Friction is preventing from the left border to get to its original position.
		box.x += bar->leftSpringX - bar->region->extents.x1;
		box.width -= bar->leftSpringX - bar->region->extents.x1;

		bar->leftMsSinceLastMove = 0;
		updateTabBar = TRUE;
	}
	
	else if(bar->leftSpeed == 0)
		bar->leftMsSinceLastMove = 0;
	
	//Right
	move = bar->rightSpeed * bar->rightMsSinceLastMove / 1000;
	
	if(move)
	{
		box.width += move;
		
		bar->rightMsSinceLastMove = 0;
		updateTabBar = TRUE;
	}
	
	else if(bar->rightSpeed == 0 && bar->region->extents.x2 != bar->rightSpringX && 
		SPRING_K * abs(bar->region->extents.x2 - bar->rightSpringX) < FRICTION)
	{
		// Friction is preventing from the right border to get to its original position.
		box.width += bar->leftSpringX - bar->region->extents.x1;

		bar->leftMsSinceLastMove = 0;
		updateTabBar = TRUE;
	}
	
	else if(bar->rightSpeed == 0)
		bar->rightMsSinceLastMove = 0;
	
	if(updateTabBar)
	{
		EMPTY_REGION(bar->region);
		XUnionRectWithRegion(&box, bar->region, bar->region);
	}
	
	for(slot = bar->slots; slot; slot = slot->next)
	{
		slot->msSinceLastMove += msSinceLastRepaint;
		move = slot->speed * slot->msSinceLastMove / 1000;
		
		if(move)
		{
			XOffsetRegion (slot->region, 
				move, 0);
			slot->msSinceLastMove = 0;
		}
		
		else if(slot->speed == 0 && (slot->region->extents.x1 + slot->region->extents.x2) / 2 != slot->springX &&
			SPRING_K * abs((slot->region->extents.x1 + slot->region->extents.x2) / 2 - slot->springX) < FRICTION)
		{
			// Friction is preventing from the slot to get to its original position.
			
			XOffsetRegion (slot->region,
				slot->springX - (slot->region->extents.x1 + slot->region->extents.x2) / 2, 0);
			slot->msSinceLastMove = 0;
		}
		
		else if(slot->speed == 0)
			slot->msSinceLastMove = 0;
	}
}

/*
 * groupInitTabBar
 *
 */
void groupInitTabBar(GroupSelection *group, CompWindow* topTab)
{
	// error
	if (group->tabBar)
		return;

	GroupTabBar *bar = (GroupTabBar*) malloc(sizeof(GroupTabBar));
	bar->slots = NULL;
	bar->nSlots = 0;
	bar->bgAnimation = AnimationNone;
	bar->bgAnimationTime = 0;
	bar->state = PaintOff;
	bar->animationTime = 0;
	bar->timeoutHandle = 0;
	bar->textLayer = NULL;
	bar->bgLayer = NULL;
	bar->selectionLayer = NULL;
	bar->hoveredSlot = NULL;
	bar->textSlot = NULL;
	bar->oldWidth = 0;
	group->tabBar = bar;

	bar->region = XCreateRegion();

	int i;
	for(i = 0; i < group->nWins; i++)
		groupCreateSlot(group, group->windows[i]);
	
	groupRecalcTabBarPos(group, WIN_X(topTab) + WIN_WIDTH(topTab) / 2,
				WIN_X(topTab), WIN_X(topTab) + WIN_WIDTH(topTab));

}

/*
 * groupDeleteTabBar
 *
 */
void groupDeleteTabBar(GroupSelection *group)
{
	GroupTabBar *bar = group->tabBar;
	
	groupDestroyCairoLayer(group->screen, bar->textLayer);
	groupDestroyCairoLayer(group->screen, bar->bgLayer);
	groupDestroyCairoLayer(group->screen, bar->selectionLayer);

	groupDestroyInputPreventionWindow(group);

	if (bar->timeoutHandle)
		compRemoveTimeout(bar->timeoutHandle);

	while (bar->slots)
		groupDeleteTabBarSlot(bar, bar->slots);

	if (bar->region)
		XDestroyRegion(bar->region);

	free(bar);
	group->tabBar = NULL;
}

/*
 * groupInitTab
 *
 */
Bool
groupInitTab(CompDisplay * d, CompAction * action, CompActionState state,
	     CompOption * option, int nOption)
{
	CompWindow *w = findWindowAtDisplay(d, d->activeWindow);
	Bool allowUntab = TRUE;
	
	if (!w)
		return TRUE;

	GROUP_WINDOW(w);
	
	if (gw->inSelection)
	{
		groupGroupWindows(d, action, state, option, nOption);
		allowUntab = FALSE;	// If the window was selected, we don't want to untab the group.
					// because the user probably wanted to tab the selected windows.
	}
	
	if (!gw->group)
		return TRUE;
	
	if (gw->group->tabbingState != PaintOff)
		groupSyncWindows(gw->group);

	if (!gw->group->tabBar) {
		groupTabGroup(w);
	} else if (allowUntab) {
		groupUntabGroup(gw->group);
	}
	
	damageScreen(w->screen);
	
	return TRUE;
}

/*
 * groupChangeTabLeft
 *
 */
Bool
groupChangeTabLeft(CompDisplay * d, CompAction * action, CompActionState state,
	       CompOption * option, int nOption)
{
	CompWindow *w = findWindowAtDisplay(d, d->activeWindow);
	CompWindow *topTab = w;
	
	if(!w)
		return TRUE;
	
	GROUP_WINDOW(w);
	GROUP_SCREEN(w->screen);
	
	if(!gw->slot || !gw->group)
		return TRUE;
	
	if(gw->group->nextTopTab)
		topTab = NEXT_TOP_TAB(gw->group);
	else if(gw->group->topTab)
		topTab = TOP_TAB(gw->group);	//If there are no tabbing animations, topTab is never NULL.
	
	gw = GET_GROUP_WINDOW(topTab, gs);

	if(gw->slot->prev)
		return groupChangeTab(gw->slot->prev, RotateLeft);
	else
		return groupChangeTab(gw->group->tabBar->revSlots, RotateLeft);
}

/*
 * groupChangeTabRight
 *
 */
Bool
groupChangeTabRight(CompDisplay * d, CompAction * action, CompActionState state,
	       CompOption * option, int nOption)
{
	CompWindow *w = findWindowAtDisplay(d, d->activeWindow);
	CompWindow *topTab = w;
	
	if(!w)
		return TRUE;
	
	GROUP_WINDOW(w);
	GROUP_SCREEN(w->screen);
	
	if(!gw->slot || !gw->group)
		return TRUE;
	
	if(gw->group->nextTopTab)
		topTab = NEXT_TOP_TAB(gw->group);
	else if(gw->group->topTab)
		topTab = TOP_TAB(gw->group);	//If there are no tabbing animations, topTab is never NULL.
	
	gw = GET_GROUP_WINDOW(topTab, gs);

	if(gw->slot->next)
		return groupChangeTab(gw->slot->next, RotateRight);
	else
		return groupChangeTab(gw->group->tabBar->slots, RotateRight);
}

/*
 * groupSwitchTopTabInput
 *
 */
void
groupSwitchTopTabInput(GroupSelection *group, Bool enable)
{
	if(!group->tabBar || !HAS_TOP_WIN(group))
		return;

	if (!group->inputPrevention)
		groupCreateInputPreventionWindow(group);

	if (!enable) 
		XMapWindow(group->screen->display->display, group->inputPrevention);
	else 
		XUnmapWindow(group->screen->display->display, group->inputPrevention);
}

/*
 * groupUpdateInputPreventionWindow
 *
 */
void
groupUpdateInputPreventionWindow(GroupSelection *group)
{
	XWindowChanges xwc; 
	
	if(!group->tabBar || !HAS_TOP_WIN(group))
		return;

	if (!group->inputPrevention)
		groupCreateInputPreventionWindow(group);

	CompWindow *w = TOP_TAB(group);

	xwc.x = group->tabBar->leftSpringX;
	xwc.y = group->tabBar->region->extents.y1; 
	xwc.width = group->tabBar->rightSpringX - group->tabBar->leftSpringX;
	xwc.height = group->tabBar->region->extents.y2 - group->tabBar->region->extents.y1; 

	xwc.stack_mode = Above;
	
	xwc.sibling = w->id;

	XConfigureWindow(group->screen->display->display, group->inputPrevention, 
			CWSibling | CWStackMode | CWX | CWY | CWWidth | CWHeight, &xwc); 
}

/*
 * groupCreateInputPreventionWindow
 *
 */
void groupCreateInputPreventionWindow(GroupSelection* group)
{
	if (!group->inputPrevention)    //For preventing a memory leak. 
	{ 
		XSetWindowAttributes attrib; 
		attrib.override_redirect = TRUE;

		group->inputPrevention = XCreateWindow(group->screen->display->display, 
				group->screen->root, -100, -100, 1, 1, 0, CopyFromParent,  
				InputOnly, CopyFromParent, CWOverrideRedirect, &attrib); 
	} 
}

/*
 * groupDestroyInputPreventionWindow
 *
 */
void groupDestroyInputPreventionWindow(GroupSelection* group)
{
	if(group->inputPrevention) 
	{ 
		XDestroyWindow(group->screen->display->display, group->inputPrevention); 

		group->inputPrevention = None; 
	}
} 

