# eykPad

<img width="600" height="400" alt="Screenshot 2026-10-10 at 11 19 01 AM" src="https://github.com/user-attachments/assets/a9a61e3a-6382-422a-8b4b-d71b42e1e1f1" />
<br><br>
<b>A multifunctional device. You can use it for:
<br><br>
   - A Timer. (Binary Mode/Regular Mode/Pomodoro Mode)<br>
   - Sequence Memorizer. (Basically Simon Says)<br>
   - Pattern Memorizer. (Memorize the pattern on the lights that show up for a split second)<br>
   - Flash Anzan. (Sequence of numbers will appear in a split second, add them up)<br>
   - Interactive Dice (Generates a Random Number.)</b>
<br><br>

For now, these are the five main functions I will focus on. 

<br>

My project mainly consists of 30 LEDS, a single-digit 7 Segment Display, and three switches. The main function for each switches are: Move right, OK, change color/boundary.

<br>

The LEDs are in a 5 by 6 matrix. I am multiplexing to save the GPIO pins. 
<br>
The top 2 rows are RED LEDs, the middle two rows are BLUE LEDs, and the last two rows are YELLOW LEDs. Since there are 10 of them each, it's easier and more efficient, since you can display numbers in base ten.

<br>

Image of the PCB in the PCB editor:<br>
<img width="600" height="400" alt="Screenshot 2026-10-10 at 11 33 52 AM" src="https://github.com/user-attachments/assets/5b608dfd-4695-4bea-ab30-b2be7d66795d" />
<br><br>
Image of the SCHEMATIC in the SCH editor:<br>
<img width="600" height="400" alt="Screenshot 2026-10-10 at 11 34 17 AM" src="https://github.com/user-attachments/assets/b9ffe5fd-00a5-475b-b2e8-cad3c7257acb" />
