import type { QuestTemplate } from '../templateTypes';
import { CHOICE_TWO_ENDINGS } from './choiceTwoEndings';
import { ESCORT } from './escort';
import { FETCH_AN_ITEM } from './fetchAnItem';
import { KILL_A_TARGET } from './killATarget';
import { TALK_TO_SOMEONE } from './talkToSomeone';

export const QUEST_TEMPLATES: readonly QuestTemplate[] = [FETCH_AN_ITEM, KILL_A_TARGET, TALK_TO_SOMEONE, ESCORT, CHOICE_TWO_ENDINGS];
